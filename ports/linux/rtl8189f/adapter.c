/*
 * @file
 * @brief First-party adapter bring-up for the RTL8189FTV HAL self-test.
 *
 * The vendor's SDIO probe (os_dep/linux/sdio_intf.c) is static and intertwined
 * with the netdev/cfg80211 registration. This file re-implements the part the
 * HAL needs, in the same order as the vendor probe:
 *
 *   SDIO enable/block size -> dvobj -> adapter -> hal ops -> chip version/config
 *   -> eFuse/chip info -> minimal driver state -> MAC -> interrupt mask
 *   -> rtw_hal_init() (chip init + firmware) -> channel
 *
 * It is GPL-2.0-only, derived from the vendor probe; the vendor tree bytes are
 * untouched. The netdev/cfg80211/MLME faces are not part of this file.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <drv_types.h>
#include <hal_data.h>
#include <hal_com.h>
#include <osdep_intf.h>
#include <rtw_io.h>
#include <rtw_pwrctrl.h>
#include <ieee80211.h>
#include <sdio_ops.h>
#include <sdio_ops_linux.h>

static _adapter *rtl8189f_adapter;
static struct delayed_work rtl8189f_dm_work;
static struct delayed_work rtl8189f_dump2_work;

void rtl8189f_dump_phy(_adapter *padapter);
void rtl8189f_dump_mac(_adapter *padapter);
u32 rtl8189f_local_hisr(void);

static void rtl8189f_dump2_fn(struct work_struct *work)
{
	if (rtl8189f_adapter != NULL) {
		pr_info("rtl8189f: idle dump\n");
		rtl8189f_dump_phy(rtl8189f_adapter);
		rtl8189f_dump_mac(rtl8189f_adapter);
	}
}
static bool rtl8189f_recv_ready;
static bool rtl8189f_sta_ready;
static unsigned long rtl8189f_irq_count;

static int rtl8189f_dump;
module_param_named(dump, rtl8189f_dump, int, 0644);
MODULE_PARM_DESC(dump, "dump MAC and PHY registers at init (debug)");

static int rtl8189f_force_ch;
module_param_named(force_ch, rtl8189f_force_ch, int, 0444);
MODULE_PARM_DESC(force_ch, "tune to this channel at init, no scan (debug)");

/* The vendor runs its dynamic check timer every two seconds; that is what
 * drives the PHY's calibration and adaptation. Without it the receiver reports
 * a CRC error for every frame: the radio hears energy but nothing demodulates. */
static void rtl8189f_dm_work_fn(struct work_struct *work)
{
	if (rtl8189f_adapter != NULL) {
		/* The vendor's dynamic check is what keeps its receiver alive,
		 * but its handler still blocks on this front end, so the
		 * watchdog is driven directly until that is understood. */
		rtw_mi_dynamic_chk_wk_hdl(rtl8189f_adapter);
		rtw_hal_dm_watchdog(rtl8189f_adapter);
		rtw_hal_periodic_tsf_update_chk(rtl8189f_adapter);
		rtw_ps_processor(rtl8189f_adapter);
		if (rtl8189f_force_ch > 0) {
			u8 data[4] = { 0 };
			int rerr;

			struct sdio_func *func =
			    adapter_to_dvobj(rtl8189f_adapter)->intf_data.func;

			sdio_claim_host(func);
			_sdio_local_read(rtl8189f_adapter,
			    SDIO_REG_RX0_REQ_LEN, 4, data);
			sdio_release_host(func);
			rerr = 0;
			pr_info("rtl8189f: idle rx0len=0x%04x hisr=0x%08x fwdl=0x%08x fwisr=0x%08x rxptr=0x%08x q=%d issued=%u\n",
			    le16_to_cpu(*(u16 *)data),
			    rtl8189f_local_hisr(),
			    rtw_read32(rtl8189f_adapter, 0x80),
			    rtw_read32(rtl8189f_adapter, 0x134),
			    rtw_read32(rtl8189f_adapter, 0x11c),
			    list_empty(&rtl8189f_adapter->cmdpriv.cmd_queue.queue) ? 0 : 1,
			    rtl8189f_adapter->cmdpriv.cmd_issued_cnt);
		}
		schedule_delayed_work(&rtl8189f_dm_work,
		    msecs_to_jiffies(2000));
	}
}

void rtl8189f_arm_dm(void)
{
	schedule_delayed_work(&rtl8189f_dm_work, msecs_to_jiffies(2000));
}

void rtl8189f_schedule_idle_dump(void)
{
	schedule_delayed_work(&rtl8189f_dump2_work, msecs_to_jiffies(3000));
}

u32 rtl8189f_rf_ch(_adapter *padapter)
{
	return PHY_QueryRFReg_8188F(padapter, RF_PATH_A, 0x00, 0xffffffff);
}

u32 rtl8189f_rf_ch_reg(_adapter *padapter, u32 reg)
{
	return PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg, 0xffffffff);
}

int rtl8189f_force_ch_get(void)
{
	return rtl8189f_force_ch;
}

bool rtl8189f_dump_on(void)
{
	return rtl8189f_dump != 0;
}

unsigned long rtl8189f_irq_count_get(void)
{
	return rtl8189f_irq_count;
}

/* The SDIO-local HIMR is the mask that decides whether the card pulls DAT1 at
 * all; read it back so a silent link can be told from a silent handler. */
/* Same-point PHY dump: the MAC side already matches the vendor, so the next
 * place a silent receiver can hide is the baseband/radio configuration. The
 * vendor trace build prints the same block at the same point in its open. */
/* Full MAC dump for a same-point diff against the vendor's own
 * /proc mac_reg_dump: any register the port leaves different shows up here. */
void rtl8189f_dump_mac(_adapter *padapter)
{
	u32 reg;

	for (reg = 0; reg < 0x800; reg += 4)
		pr_info("MAC%04x %08x\n", reg, rtw_read32(padapter, reg));
}

void rtl8189f_dump_phy(_adapter *padapter)
{
	u32 reg;

	for (reg = 0x800; reg < 0xc00; reg += 32)
		pr_info("rtl8189f: BB%03x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		    reg,
		    PHY_QueryBBReg_8188F(padapter, reg + 0, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 4, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 8, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 12, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 16, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 20, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 24, 0xffffffff),
		    PHY_QueryBBReg_8188F(padapter, reg + 28, 0xffffffff));
	for (reg = 0x00; reg < 0x40; reg += 8)
		pr_info("rtl8189f: RFA %02x %05x %05x %05x %05x %05x %05x %05x %05x\n",
		    reg,
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 0, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 1, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 2, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 3, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 4, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 5, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 6, 0xffffffff),
		    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, reg + 7, 0xffffffff));
}

unsigned int rtl8189f_local_hisr(void)
{
	u8 buf[4] = { 0 };
	u32 hisr = 0;
	struct sdio_func *func;

	if (rtl8189f_adapter == NULL)
		return 0;
	func = adapter_to_dvobj(rtl8189f_adapter)->intf_data.func;
	if (func == NULL)
		return 0;
	sdio_claim_host(func);
	if (_sdio_local_read(rtl8189f_adapter, 0x18, 4, buf) == 0) {
		memcpy(&hisr, buf, sizeof(hisr));
		hisr = le32_to_cpu(hisr);
	}
	sdio_release_host(func);
	return hisr;
}

unsigned int rtl8189f_local_himr(void)
{
	u8 buf[4] = { 0 };
	u32 himr = 0;

	struct sdio_func *func;
	s32 rerr = -1;

	if (rtl8189f_adapter == NULL)
		return 0;
	func = adapter_to_dvobj(rtl8189f_adapter)->intf_data.func;
	if (func == NULL)
		return 0;
	/* The raw IO helpers do not claim the host themselves; doing it here
	 * keeps the mmc core from warning (and from running the read unowned). */
	sdio_claim_host(func);
	rerr = _sdio_local_read(rtl8189f_adapter, 0x14, 4, buf);
	if (rerr == 0) {
		memcpy(&himr, buf, sizeof(himr));
		himr = le32_to_cpu(himr);
	}
	sdio_release_host(func);
	pr_info("rtl8189f: himr read err=%d raw=%02x%02x%02x%02x\n",
	    rerr, buf[3], buf[2], buf[1], buf[0]);
	return himr;
}

/* The SDIO IRQ thread calls this with the host already claimed by the mmc
 * layer. The vendor tells its raw IO helpers that they run on that thread
 * (sdio_intf.c:162-164); without it the first HISR read tries to claim the
 * host again and self-deadlocks. */
static void rtl8189f_sdio_irq(struct sdio_func *func)
{
	if (rtl8189f_adapter != NULL) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(rtl8189f_adapter);

		rtl8189f_irq_count++;
		rtw_sdio_set_irq_thd(dvobj, current);
		sd_int_hdl(rtl8189f_adapter);
		rtw_sdio_set_irq_thd(dvobj, NULL);
	}
}

_adapter *rtl8189f_adapter_get(void)
{
	return rtl8189f_adapter;
}

static const struct rtw_if_operations rtl8189f_sdio_if_ops = {
	.read	= rtw_sdio_raw_read,
	.write	= rtw_sdio_raw_write,
};

/* A subset of the vendor devobj_init(): the locks and counters the SDIO
 * read/write path and the HAL use. Timers and the optional feature blocks are
 * not started; the front-end does not run the vendor MLME. */
static struct dvobj_priv *rtl8189f_dvobj_alloc(void)
{
	struct dvobj_priv *dvobj;

	dvobj = rtw_zvmalloc(sizeof(*dvobj));
	if (dvobj == NULL)
		return NULL;

	_rtw_mutex_init(&dvobj->hw_init_mutex);
	_rtw_mutex_init(&dvobj->h2c_fwcmd_mutex);
	_rtw_mutex_init(&dvobj->setch_mutex);
	_rtw_mutex_init(&dvobj->setbw_mutex);
	_rtw_mutex_init(&dvobj->rf_read_reg_mutex);
	_rtw_mutex_init(&dvobj->ioctrl_mutex);
	_rtw_spinlock_init(&dvobj->cam_ctl.lock);
	_rtw_mutex_init(&dvobj->cam_ctl.sec_cam_access_mutex);
	dvobj->processing_dev_remove = _FALSE;
	ATOMIC_SET(&dvobj->disable_func, 0);
	dvobj->intf_ops = &rtl8189f_sdio_if_ops;
	return dvobj;
}

/* The vendor sdio_init(): claim, enable, 512-byte blocks, intf_data. */
static u32 rtl8189f_sdio_init(struct dvobj_priv *dvobj)
{
	PSDIO_DATA psdio = &dvobj->intf_data;
	struct sdio_func *func = psdio->func;
	int err;

	sdio_claim_host(func);
	err = sdio_enable_func(func);
	if (err) {
		pr_err("rtl8189f: sdio_enable_func failed (%d)\n", err);
		goto release;
	}
	err = sdio_set_block_size(func, 512);
	if (err) {
		pr_err("rtl8189f: sdio_set_block_size failed (%d)\n", err);
		goto release;
	}
	psdio->block_transfer_len = 512;
	psdio->tx_block_mode = 1;
	psdio->rx_block_mode = 1;
	psdio->card = func->card;
	psdio->timing = func->card->host->ios.timing;
	psdio->clock = func->card->host->ios.clock;
	psdio->func_number = func->card->sdio_funcs;
	psdio->sd3_bus_mode = _FALSE;

release:
	sdio_release_host(func);
	return err ? _FAIL : _SUCCESS;
}

static void rtl8189f_sdio_deinit(struct dvobj_priv *dvobj)
{
	struct sdio_func *func = dvobj->intf_data.func;
	int err;

	if (func == NULL)
		return;
	sdio_claim_host(func);
	err = sdio_disable_func(func);
	if (err)
		pr_err("rtl8189f: sdio_disable_func failed (%d)\n", err);
	sdio_release_host(func);
}

/* The vendor loadparam() sets ~200 registry defaults from module parameters.
 * The self-test only needs the station/2.4 GHz/LPS-off subset. */
static void rtl8189f_registry_defaults(_adapter *padapter)
{
	struct registry_priv *preg = &padapter->registrypriv;

	preg->wireless_mode = WIRELESS_MODE_24G;
	preg->channel = 1;
	preg->ht_enable = 1;
	preg->bw_mode = CHANNEL_WIDTH_20;
	preg->ampdu_enable = 1;
	preg->power_mgnt = PS_MODE_ACTIVE;
	preg->ips_mode = IPS_NONE;
	preg->lps_level = 0;
	preg->mp_mode = 0;
	preg->wifi_spec = 0;
	preg->notch_filter = 0;
	preg->software_encrypt = 0;
	preg->software_decrypt = 0;
}

/* The vendor rtw_init_drv_sw() starts the cmd/MLME/AP/P2P state. The HAL
 * bring-up only needs the MACID/CAM limits and the channel/LPS defaults. */
static u8 rtl8189f_drv_sw_init(_adapter *padapter)
{
	struct hal_spec_t *spec = GET_HAL_SPEC(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	dvobj->macid_ctl.num = rtw_min(spec->macid_num, MACID_NUM_SW_LIMIT);
	dvobj->cam_ctl.sec_cap = spec->sec_cap;
	dvobj->cam_ctl.num = rtw_min(spec->sec_cam_ent_num, SEC_CAM_ENT_NUM_SW_LIMIT);

	padapter->mlmeextpriv.cur_channel = 1;
	padapter->mlmeextpriv.cur_bwmode = CHANNEL_WIDTH_20;
	padapter->mlmeextpriv.cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	pwrpriv->power_mgnt = PS_MODE_ACTIVE;
	pwrpriv->ips_mode = IPS_NONE;
	pwrpriv->lps_level = 0;

	/* The vendor's rtw_init_default_value() enables both data-path bits and
	 * marks the netdev open; without them RX/TX are refused before the HAL
	 * ever looks at the card. */
	RTW_ENABLE_FUNC(padapter, DF_RX_BIT);
	RTW_ENABLE_FUNC(padapter, DF_TX_BIT);
	padapter->net_closed = _FALSE;

	/* The vendor calls this from rtw_init_drv_sw() before any HAL init;
	 * it hooks the registry into phydm (mp_mode, interface, RF type). */
	rtw_hal_dm_init(padapter);
	return _SUCCESS;
}

static u8 rtl8189f_set_hal_ops(_adapter *padapter)
{
	if (rtw_hal_data_init(padapter) == _FAIL)
		return _FAIL;

	if (rtw_get_chip_type(padapter) == RTL8188F)
		rtl8188fs_set_hal_ops(padapter);

	if (rtw_hal_ops_check(padapter) == _FAIL)
		return _FAIL;
	if (hal_spec_init(padapter) == _FAIL)
		return _FAIL;
	return _SUCCESS;
}

static _adapter *rtl8189f_adapter_alloc(struct dvobj_priv *dvobj)
{
	_adapter *padapter;

	padapter = rtw_zvmalloc(sizeof(*padapter));
	if (padapter == NULL)
		return NULL;

	rtl8189f_registry_defaults(padapter);
	padapter->dvobj = dvobj;
	rtw_set_drv_stopped(padapter);
	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;
	padapter->isprimary = _TRUE;
	padapter->adapter_type = PRIMARY_ADAPTER;
	padapter->hw_port = HW_PORT0;

	if (rtl8189f_set_hal_ops(padapter) == _FAIL)
		goto err;
	if (rtw_init_io_priv(padapter, sdio_set_intf_ops) == _FAIL)
		goto err_hal;

	rtw_hal_read_chip_version(padapter);
	rtw_hal_chip_configure(padapter);
	if (rtw_hal_read_chip_info(padapter) == _FAIL)
		goto err_hal;
	if (rtl8189f_drv_sw_init(padapter) == _FAIL)
		goto err_hal;

	rtw_macaddr_cfg(&dvobj->intf_data.func->dev, adapter_mac_addr(padapter),
	    get_hal_mac_addr(padapter));
	rtw_hal_disable_interrupt(padapter);
	return padapter;

err_hal:
	rtw_hal_free_data(padapter);
err:
	rtw_vmfree(padapter, sizeof(*padapter));
	return NULL;
}

static int rtl8189f_hal_start(_adapter *padapter)
{
	/* Vendor order (rtw_init_drv_sw at probe, rtw_hal_init at open): the
	 * core receive init, which itself runs the HAL's SDIO buffer setup,
	 * happens before the firmware/HAL init. */
	if (_rtw_init_cmd_priv(&padapter->cmdpriv) == _FAIL) {
		pr_err("rtl8189f: _rtw_init_cmd_priv failed\n");
		return -ENOMEM;
	}
	/* rtw_enqueue_cmd() resolves the adapter from the queue itself; the
	 * vendor's init leaves this null, which null-dereferences as soon as a
	 * command is queued from another context. */
	padapter->cmdpriv.padapter = padapter;
	if (_rtw_init_recv_priv(&padapter->recvpriv, padapter) == _FAIL) {
		pr_err("rtl8189f: _rtw_init_recv_priv failed\n");
		return -ENOMEM;
	}
	rtl8189f_recv_ready = true;
	if (_rtw_init_sta_priv(&padapter->stapriv) == _FAIL) {
		pr_err("rtl8189f: _rtw_init_sta_priv failed\n");
		return -ENOMEM;
	}
	rtl8189f_sta_ready = true;

	/* The vendor's _rtw_init_mlme_priv() seeds the station state, and
	 * rtw_hal_init() only sends the opmode H2C when a state is present.
	 * Without it the firmware is never told it is a station. */
	init_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE);

	/* The vendor initializes power control in rtw_init_drv_sw(); without
	 * it the power lock is uninitialized and the power-save processing
	 * blocks the first time it runs. */
	rtw_init_pwrctrl_priv(padapter);

	/* The vendor clears the stopped flag in _netdev_open() before the HAL
	 * init (os_intfs.c:3748); sd_int_hdl() refuses to run while it is set. */
	rtw_clr_drv_stopped(padapter);

	/* rtw_init_drv_sw() order: xmit, recv, sta, all before rtw_hal_init().
	 * The core recv init builds free_recv_queue and the frame pool; the HAL
	 * recv init (below) only adds the SDIO buffers. Without the core one the
	 * first received frame dereferences an empty queue. */
	if (rtw_hal_init(padapter) != _SUCCESS) {
		pr_err("rtl8189f: rtw_hal_init failed\n");
		return -EIO;
	}
	/* The vendor's software init lays down the transmit state before the
	 * interface is opened; the management frame path needs its buffers and
	 * queue even when the port sends nothing yet. */
	if (_rtw_init_xmit_priv(&padapter->xmitpriv, padapter) == _SUCCESS)
		pr_info("rtl8189f: xmit state ready\n");
	else
		pr_err("rtl8189f: _rtw_init_xmit_priv failed\n");
	pr_info("rtl8189f: firmware/hal up, mac %pM\n",
	    adapter_mac_addr(padapter));

	if (rtl8189f_dump) {
		rtl8189f_dump_phy(padapter);
		rtl8189f_dump_mac(padapter);
	}


	{
		struct sdio_func *func = adapter_to_dvobj(padapter)->intf_data.func;
		int irq_ret;

		rtw_stop_cmd_thread(padapter);
		sdio_claim_host(func);
		irq_ret = sdio_claim_irq(func, rtl8189f_sdio_irq);
		sdio_release_host(func);
		if (irq_ret != 0) {
			pr_err("rtl8189f: sdio_claim_irq failed (%d)\n", irq_ret);
			return -EIO;
		}
		pr_info("rtl8189f: sdio irq claimed\n");
	}
	{
		u8 pwr = _FALSE;
		rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &pwr);
		pr_info("rtl8189f: hal sdio_himr=0x%08x apfm_on_mac=%u\n",
		    GET_HAL_DATA(padapter)->sdio_himr, pwr);
	}

	pr_info("rtl8189f: rf ch=0x%05x rf18=0x%05x\n",
	    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, 0x00, 0xffffffff),
	    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, 0x18, 0xffffffff));
	pr_info("rtl8189f: mac CR=0x%08x RCR=0x%08x MSR=0x%02x\n",
	    rtw_read32(padapter, 0x100), rtw_read32(padapter, 0x608),
	    rtw_read8(padapter, 0x102));
	pr_info("rtl8189f: rxfilt=%04x/%04x/%04x agg=0x%03x fwdl=0x%05x\n",
	    rtw_read16(padapter, 0x6a0), rtw_read16(padapter, 0x6a2),
	    rtw_read16(padapter, 0x6a4), rtw_read16(padapter, 0x280),
	    rtw_read32(padapter, 0x80));
	rtw_hal_enable_interrupt(padapter);
	pr_info("rtl8189f: local HIMR after enable: 0x%08x\n",
	    rtl8189f_local_himr());



	set_channel_bwmode(padapter, 5, HAL_PRIME_CHNL_OFFSET_DONT_CARE,
	    CHANNEL_WIDTH_20);
	pr_info("rtl8189f: channel 5 programmed\n");
	INIT_DELAYED_WORK(&rtl8189f_dm_work, rtl8189f_dm_work_fn);
	schedule_delayed_work(&rtl8189f_dm_work, msecs_to_jiffies(2000));
	if (rtl8189f_dump) {
		INIT_DELAYED_WORK(&rtl8189f_dump2_work, rtl8189f_dump2_fn);
		schedule_delayed_work(&rtl8189f_dump2_work, msecs_to_jiffies(4000));
	}
	pr_info("rtl8189f: dm watchdog scheduled\n");
	if (rtl8189f_force_ch > 0) {
		set_channel_bwmode(padapter, rtl8189f_force_ch,
		    HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		pr_info("rtl8189f: forced channel %d rf0=%05x\n",
		    rtl8189f_force_ch, rtl8189f_rf_ch(padapter));
	}
	pr_info("rtl8189f: rf ch after tuning=0x%05x\n",
	    PHY_QueryRFReg_8188F(padapter, RF_PATH_A, 0x00, 0xffffffff));
	return 0;
}

int rtl8189f_attach(struct sdio_func *func)
{
	struct dvobj_priv *dvobj;
	_adapter *padapter;

	if (rtl8189f_adapter != NULL)
		return -EBUSY;

	dvobj = rtl8189f_dvobj_alloc();
	if (dvobj == NULL)
		return -ENOMEM;
	dvobj->intf_data.func = func;
	sdio_set_drvdata(func, dvobj);
	if (rtl8189f_sdio_init(dvobj) != _SUCCESS)
		goto err_dvobj;

	dvobj->chip_type = RTL8188F;
	dvobj->HardwareType = HARDWARE_TYPE_RTL8188FS;
	dvobj->interface_type = RTW_SDIO;
	rtw_reset_continual_io_error(dvobj);

	padapter = rtl8189f_adapter_alloc(dvobj);
	if (padapter == NULL)
		goto err_sdio;
	rtl8189f_adapter = padapter;

	pr_info("rtl8189f: adapter ready, mac %pM\n",
	    adapter_mac_addr(padapter));
	if (rtl8189f_hal_start(padapter) != 0)
		goto err_adapter;
	return 0;

err_adapter:
	rtl8189f_adapter = NULL;
	rtw_hal_free_data(padapter);
	rtw_vmfree(padapter, sizeof(*padapter));
err_sdio:
	rtl8189f_sdio_deinit(dvobj);
err_dvobj:
	sdio_set_drvdata(func, NULL);
	rtw_vmfree(dvobj, sizeof(*dvobj));
	return -ENODEV;
}

void rtl8189f_detach(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	_adapter *padapter = rtl8189f_adapter;

	if (padapter != NULL) {
		struct sdio_func *func = adapter_to_dvobj(padapter)->intf_data.func;

		cancel_delayed_work_sync(&rtl8189f_dm_work);
		pr_info("rtl8189f: detach: disable interrupts\n");
		rtw_hal_disable_interrupt(padapter);
		if (func != NULL) {
			/* sdio_release_irq() may sleep and needs the host, exactly
			 * like the claim in the vendor's sdio_deinit(). */
			pr_info("rtl8189f: detach: release irq\n");
			sdio_claim_host(func);
			sdio_release_irq(func);
			sdio_release_host(func);
		}
		if (rtl8189f_recv_ready) {
			/* _rtw_init_recv_priv() starts a 2 s signal-stat timer and
			 * the vendor cancels it in rtw_free_drv_sw(); leaving it
			 * running panics the timer wheel after the module text
			 * goes away (measured 2026-10-20). */
			pr_info("rtl8189f: detach: cancel signal timer\n");
			_cancel_timer_ex(&padapter->recvpriv.signal_stat_timer);
			pr_info("rtl8189f: detach: free recv priv\n");
			/* also frees the HAL's SDIO buffers */
			_rtw_free_recv_priv(&padapter->recvpriv);
			rtl8189f_recv_ready = false;
		}
		if (rtl8189f_sta_ready) {
			pr_info("rtl8189f: detach: free sta priv\n");
			_rtw_free_sta_priv(&padapter->stapriv);
			rtl8189f_sta_ready = false;
		}
		pr_info("rtl8189f: detach: hal deinit\n");
		rtw_hal_deinit(padapter);
		rtw_hal_free_data(padapter);
		rtl8189f_adapter = NULL;
		rtw_vmfree(padapter, sizeof(*padapter));
	}
	if (dvobj != NULL) {
		rtl8189f_sdio_deinit(dvobj);
		sdio_set_drvdata(func, NULL);
		rtw_vmfree(dvobj, sizeof(*dvobj));
	}
}