extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int b=10;
int f(int x) {
  PRINT(x);
  if (x > 0) 
  	return 5 + f(x - 5);
  else 
    return 0;
}
int main() {
  // int b=6;
   PRINT(f(b));
}


#10