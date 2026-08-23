/* Phase 3: 仅顶层 _Static_assert (无 struct, 无 sizeof) */
_Static_assert(1, "always true");
_Static_assert(4 == 4, "4 equals 4");

int main(void) {
    return 0;
}
