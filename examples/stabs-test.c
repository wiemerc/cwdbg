int a = 1;
static short b = 2;
static const char *dummy = "bla";

int f1(const char *s)
{
    int a = 2;
    int c = b;

    int f2()
    {
        int d = c;

        void f3(int x)
        {
            int y;

            for (y = 0; y < 10; y++) {
                ++x;
            }
        }

        {
            int e = 3;
        }
        ++d;
        return 0;
    }

    f2();
    ++c;
    return 0;
}
