/* @file    compilefail.c
 * @brief   Fixture: a negative source that must not compile, the form a compile-time
 *          guard takes when the guard itself is what is being tested.
 * @author  zhugengyu
 * @date    07.10.2026
 * @design test double - ref net80211:tests/ccm.c:1
 */
#error "this source is a negative fixture and must never compile"

int main( void )
{
    return 0;
}
