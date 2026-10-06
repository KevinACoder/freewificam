/* @file    crash.c
 * @brief   Fixture: a designed hard crash, the form a deliberate misuse case takes.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design test double - ref net80211:tests/ccm.c:1
 */
#include <stdio.h>

int main( void )
{
    printf( "mock-selftest-fixture: about to crash as designed\n" );
    fflush( stdout );

    *( volatile int * ) 0 = 1;
    return 0;
}
