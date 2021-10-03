extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int f() {
    int b;
    b = 1211;
    return b;
}

int main() {
    int a;
    a = f();
    PRINT(a);
}