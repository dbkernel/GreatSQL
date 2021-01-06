/* Copyright (c) 2011, 2021, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA */

#include "my_config.h"
#include <gtest/gtest.h>

#include "test_utils.h"

#include <my_decimal.h>

namespace my_decimal_unittest {

using my_testing::chars_2_decimal;
using my_testing::Server_initializer;
using my_testing::Mock_error_handler;

class DecimalTest : public ::testing::Test
{
protected:
  virtual void SetUp() { initializer.SetUp(); }
  virtual void TearDown() { initializer.TearDown(); }

  THD *thd() { return initializer.thd(); }

  Server_initializer initializer;

  my_decimal d1;
  my_decimal d2;
};


TEST_F(DecimalTest, CopyAndCompare)
{
  ulonglong val= 42;
  EXPECT_EQ(0, ulonglong2decimal(val, &d1));

  d2= d1;                                       // operator=()
  my_decimal d3(d1);                            // Copy constructor.

  EXPECT_EQ(0, my_decimal_cmp(&d1, &d2));
  EXPECT_EQ(0, my_decimal_cmp(&d2, &d3));
  EXPECT_EQ(0, my_decimal_cmp(&d3, &d1));

  ulonglong val1, val2, val3;
  EXPECT_EQ(0, decimal2ulonglong(&d1, &val1));
  EXPECT_EQ(0, decimal2ulonglong(&d2, &val2));
  EXPECT_EQ(0, decimal2ulonglong(&d3, &val3));
  EXPECT_EQ(val, val1);
  EXPECT_EQ(val, val2);
  EXPECT_EQ(val, val3);

  // The CTOR/operator=() generated by the compiler would fail here:
  val= 45;
  EXPECT_EQ(0, ulonglong2decimal(val, &d1));
  EXPECT_EQ(1, my_decimal_cmp(&d1, &d2));
  EXPECT_EQ(1, my_decimal_cmp(&d1, &d3));
}


TEST_F(DecimalTest, RoundOverflow)
{
  const char arg_str[]= "999999999";
  String str(arg_str, &my_charset_bin);

  EXPECT_EQ(E_DEC_OK,
            string2my_decimal(E_DEC_FATAL_ERROR, &str, &d1));
  d1.sanity_check();

  for (int ix= 0; ix < DECIMAL_MAX_POSSIBLE_PRECISION; ++ix)
  {
    my_decimal d3;
    const int expect=
      (ix + str.length() <= DECIMAL_MAX_POSSIBLE_PRECISION) ?
      E_DEC_OK : E_DEC_TRUNCATED;
    const bool do_truncate= true;
    EXPECT_EQ(expect,
              my_decimal_round(E_DEC_FATAL_ERROR, &d1, ix, do_truncate, &d3))
      << "ix:" << ix;
    d3.sanity_check();
    EXPECT_EQ(0, my_decimal_cmp(&d1, &d3));
  }
}


TEST_F(DecimalTest, Swap)
{
  ulonglong val1= 1;
  ulonglong val2= 2;
  EXPECT_EQ(0, ulonglong2decimal(val1, &d1));
  EXPECT_EQ(0, ulonglong2decimal(val2, &d2));
  my_decimal d1copy(d1);
  my_decimal d2copy(d2);
  EXPECT_EQ(0, my_decimal_cmp(&d1, &d1copy));
  EXPECT_EQ(0, my_decimal_cmp(&d2, &d2copy));
  d1.swap(d2);
  EXPECT_EQ(0, my_decimal_cmp(&d2, &d1copy));
  EXPECT_EQ(0, my_decimal_cmp(&d1, &d2copy));
}



TEST_F(DecimalTest, Multiply)
{
  const char arg1[]=
    "000000001."
    "10000000000000000000" "00000000000000000000" "00000000000000000000"
    "000000000000";
  const char arg2[]= "1.75";
  char buff[DECIMAL_MAX_STR_LENGTH];
  int bufsz;
  my_decimal prod;

  EXPECT_EQ(E_DEC_OK, chars_2_decimal(arg1, &d1));
  EXPECT_EQ(E_DEC_OK, chars_2_decimal(arg2, &d2));

  // Limit the precision, otherwise "1.75" will be truncated to "1."
  set_if_smaller(d1.frac, NOT_FIXED_DEC);
  set_if_smaller(d2.frac, NOT_FIXED_DEC);
  EXPECT_EQ(0, my_decimal_mul(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                              &prod, &d1, &d2));
  EXPECT_EQ(NOT_FIXED_DEC, d1.frac);
  EXPECT_EQ(2, d2.frac);
  EXPECT_EQ(NOT_FIXED_DEC, prod.frac);
  bufsz= sizeof(buff);
  EXPECT_EQ(0, decimal2string(&prod, buff, &bufsz, 0, 0, 0));
  EXPECT_STREQ("1.9250000000000000000000000000000", buff);
}


/*
  This is a simple iterative implementation based on addition and subtraction,
  for verifying the result of decimal_mod().

  decimal_mod() says:
  DESCRIPTION
    the modulus R in    R = M mod N
    is defined as

    0 <= |R| < |M|
    sign R == sign M
    R = M - k*N, where k is integer

    thus, there's no requirement for M or N to be integers
 */
int decimal_modulo(uint mask,
                   my_decimal *res,
                   const my_decimal *m,
                   const my_decimal *n)
{
  my_decimal abs_m(*m);
  my_decimal abs_n(*n);
  abs_m.sign(false);
  abs_n.sign(false);

  my_decimal r;
  my_decimal k1(abs_n);
  my_decimal kn(decimal_zero);
  my_decimal next_r(abs_m);
  int ret;
  do
  {
    r= next_r;

    my_decimal res;
    if ((ret= my_decimal_add(E_DEC_FATAL_ERROR, &res, &k1, &kn)) != E_DEC_OK)
    {
      ADD_FAILURE();
      return ret;
    }
    kn= res;

    if ((ret= my_decimal_sub(E_DEC_FATAL_ERROR,
                             &next_r, &abs_m, &kn) != E_DEC_OK))
    {
      ADD_FAILURE();
      return ret;
    }
  } while (my_decimal_cmp(&next_r, &decimal_zero) >= 0);
  r.sign(m->sign());
  *res= r;
  return 0;
}


struct Mod_data
{
  const char *a;
  const char *b;
  const char *result;
};

Mod_data mod_test_input[]=
{
  { "234"     , "10",      "4"      },
  { "234.567" , "10.555",  "2.357"  },
  { "-234.567", "10.555",  "-2.357" },
  { "234.567" , "-10.555", "2.357"  },
  { "-234.567", "-10.555", "-2.357" },
  { "999"     , "0.1",     "0.0"    },
  { "999"     , "0.7",     "0.1"    },
  { "10"      , "123",     "10"     },
  { NULL, NULL, NULL}
};


TEST_F(DecimalTest, Modulo)
{
  my_decimal expected_result;
  my_decimal xxx_result;
  my_decimal mod_result;
  char buff_x[DECIMAL_MAX_STR_LENGTH];
  char buff_m[DECIMAL_MAX_STR_LENGTH];

  for (Mod_data *pd= mod_test_input; pd->a; ++pd)
  {
    int bufsz_x= sizeof(buff_x);
    int bufsz_m= sizeof(buff_m);

    EXPECT_EQ(0, chars_2_decimal(pd->a, &d1));
    EXPECT_EQ(0, chars_2_decimal(pd->b, &d2));
    EXPECT_EQ(0, chars_2_decimal(pd->result, &expected_result));

    EXPECT_EQ(0, my_decimal_mod(E_DEC_FATAL_ERROR, &mod_result, &d1, &d2));
    EXPECT_EQ(0, decimal2string(&mod_result, buff_m, &bufsz_m, 0, 0, 0));
    EXPECT_EQ(0, my_decimal_cmp(&expected_result, &mod_result))
      << " a:" << pd->a
      << " b:" << pd->b
      << " expected:" << pd->result
      << " got mod:" << buff_m
      ;

    EXPECT_EQ(0, decimal_modulo(E_DEC_FATAL_ERROR, &xxx_result, &d1, &d2));
    EXPECT_EQ(0, decimal2string(&xxx_result, buff_x, &bufsz_x, 0, 0, 0));
    EXPECT_EQ(0, my_decimal_cmp(&expected_result, &xxx_result))
      << " a:" << pd->a
      << " b:" << pd->b
      << " expected:" << pd->result
      << " got mod:" << buff_m
      << " got xxx:" << buff_x
      ;
  }
}


// Verifies that decimal_mul() does not return negative zero.
TEST_F(DecimalTest, NegativeZeroMultiply)
{
  EXPECT_EQ(E_DEC_OK, chars_2_decimal("0.0", &d1));
  EXPECT_EQ(E_DEC_OK, chars_2_decimal("0.0", &d2));
  EXPECT_EQ(0, my_decimal_cmp(&d1, &decimal_zero));
  EXPECT_EQ(0, my_decimal_cmp(&d2, &decimal_zero));
  d1.sign(true);
  my_decimal product;
  EXPECT_EQ(E_DEC_OK, decimal_mul(&d1, &d2, &product));
  EXPECT_FALSE(product.sign());
  EXPECT_EQ(0, my_decimal_cmp(&product, &decimal_zero));
}


// Verifies that decimal_add() *does* return negative zero.
TEST_F(DecimalTest, NegativeZeroAdd)
{
  EXPECT_EQ(E_DEC_OK, chars_2_decimal("0.0", &d1));
  EXPECT_EQ(E_DEC_OK, chars_2_decimal("0.0", &d2));
  EXPECT_EQ(0, my_decimal_cmp(&d1, &decimal_zero));
  EXPECT_EQ(0, my_decimal_cmp(&d2, &decimal_zero));
  d1.sign(true);
  d2.sign(true);
  my_decimal sum;
  EXPECT_EQ(E_DEC_OK, decimal_add(&d1, &d2, &sum));
  EXPECT_TRUE(sum.sign());
  // This one will DBUG_ASSERT
  // EXPECT_EQ(0, my_decimal_cmp(&sum, &decimal_zero));
}


TEST_F(DecimalTest, BinaryConversion)
{
  const int prec= 60;
  const int scale= 0;
  EXPECT_EQ(E_DEC_OK, chars_2_decimal("000000000", &d1));
  int binary_size= my_decimal_get_binary_size(prec, scale);
  uchar *bin= new uchar[binary_size];

  // Convert to binary, and back.
  EXPECT_EQ(E_DEC_OK, my_decimal2binary(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                                        &d1, bin, prec, scale));
  EXPECT_EQ(E_DEC_OK, binary2my_decimal(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                                        bin, &d2, prec, scale));
  EXPECT_GT(d2.precision(), 0U);
  EXPECT_EQ(0, my_decimal_cmp(&d1, &d2));

  // 0.0 * 0.0
  my_decimal product;
  EXPECT_EQ(E_DEC_OK, my_decimal_mul(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                                     &product, &d2, &d2));
  // 0.0 * (-0.0)
  my_decimal neg_prod;
  my_decimal d3(d2);
  d3.sign(true);
  EXPECT_EQ(E_DEC_OK, my_decimal_mul(E_DEC_FATAL_ERROR & ~E_DEC_OVERFLOW,
                                     &neg_prod, &d2, &d3));
  delete[] bin;
}

}
