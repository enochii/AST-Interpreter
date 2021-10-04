extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

// simple call handling
int f(int x) {
    int b = 1211;
    return b;
}

int main() {
    int a;
    a = f(10);
    PRINT(a);
}