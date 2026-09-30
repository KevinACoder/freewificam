# freewebcam —— 多配置构建基线
# 用法: make CONFIG=<名> all|deploy|gates
# R1 起 configs/ 提供构建配置；本文件当前为骨架占位。

CONFIG ?= nocache
BUILD_DIR := build/$(CONFIG)

.PHONY: all deploy gates clean help

all:
	@echo "freewebcam: no buildable target yet (baseline skeleton); CONFIG=$(CONFIG) -> $(BUILD_DIR)"

deploy:
	@echo "deploy: nothing to deploy yet"

gates:
	@echo "gates: nothing to scan yet"

clean:
	rm -rf build/

help:
	@echo "make CONFIG=<名> all|deploy|gates|clean"
