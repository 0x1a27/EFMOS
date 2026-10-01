/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EFMOS - a 64-bit x86_64 UEFI operating system written in C.
 *
 * Copyright (C) 2026 0x1a27
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* 编译器新特性最小测试: _Static_assert + float/double + struct 位字段
 * 编译: efcc /Program/efcc/efcc_test.c -o /Program/efcc/efcc_test.efs -T 0x800000
 */

_Static_assert(1, "always true");
_Static_assert(4 == 4);
_Static_assert(sizeof(int) == 4, "int must be 4 bytes");

struct Flags {
    unsigned f1 : 3;
    unsigned f2 : 5;
    unsigned f3 : 8;
    int      normal;
};

_Static_assert(sizeof(struct Flags) == 8, "Flags size mismatch");

float  g_f = 3.14f;
double g_d = 2.71828e0;

int main(void) {
    _Static_assert(sizeof(double) == 8, "block-scope static assert");

    int a = (int)g_f;
    double b = (double)a + g_d;
    int c = (int)(b * g_f);
    if (a + c == 0) return 1;

    if (g_f < 0.0f) return 2;
    if (g_d > 0.0) return 0;

    struct Flags fl;
    fl.f1 = 7;
    fl.f2 = 31;
    fl.f3 = 255;
    fl.f1 += 1;
    fl.f2++;
    fl.normal = fl.f3 + fl.f1;
    if (fl.normal <= 0) return 3;

    return (int)(g_d + g_f + fl.f2 + a);
}
