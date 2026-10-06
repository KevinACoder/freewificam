/* @file    fail.c
 * @brief   Fixture: a run that fails on purpose, with a signature to match.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design test double - ref net80211:tests/ccm.c:1
 */
#include <stdio.h>

int main( void )
{
    printf( "mock-selftest-fixture: red as designed reason=4 w=da7a\n" );
    return 1;
}
