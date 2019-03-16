/* Fixed point operations used in mlfqs.
   This follows the 17.14 fixed point specification. */

/* Decimal point specification constant */
#define F (1 << 14)

int int_to_fp(int n);
int fp_to_int_round(int x);
int fp_to_int(int x);
int fp_mul(int x, int y);
int fp_div(int x, int y);
int fp_add_int(int x, int n);
int fp_sub_int(int x, int n);
int fp_mul_int(int x, int n);
int fp_div_int(int x, int n);

int int_to_fp(int n)
{
  return n * F;
}

int fp_to_int_round(int x)
{
  if (x >= 0)
    {
      return (x + F / 2) / F;
    }
  else
    {
      return (x - F / 2) / F;
    }
}

int fp_to_int(int x)
{
  return x / F;
}

int fp_mul(int x, int y)
{
  return ((int64_t) x) * y / F;
}

int fp_div(int x, int y)
{
  return ((int64_t) x) * F / y;
}

/* Operations between a fixed point number x and integer n */
int fp_add_int(int x, int n)
{
  return x + int_to_fp(n);
}

int fp_sub_int(int x, int n)
{
  return x - int_to_fp(n);
}

int fp_mul_int(int x, int n)
{
  return x * n;
}

int fp_div_int(int x, int n)
{
  return x / n;
}
