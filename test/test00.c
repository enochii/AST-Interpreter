extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int main() {
   // int a = GET(); // TODO:
   // PRINT(1);
   int a;
   a = GET();
   // a=100;
   PRINT(a);
   a = 11;
   PRINT(a);
}
