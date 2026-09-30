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

/* Phase 2: 测试全局变量 (float/double/int) + cast + 运行输出 */
float  g_f = 3.14f;
double g_d = 2.71828;
int    g_i = 42;

int main(void) {
    print("gcc_float running\n");
    int a = (int)g_f;
    double b = (double)a + g_d;
    if (b > 0.0) print("b > 0\n");
    return g_i;
}
