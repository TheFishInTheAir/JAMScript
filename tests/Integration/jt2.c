int compyou(char *s);

jtask* localme(int c, char *s) {
    while(1) {
        //jsleep(2000);
        printf("############-->>> Hello  ME  %d... %s\n", c, s);
    }
}

jtask* localyou(int c, char *s) {
    int x;
    while(1) {
        jsleep(100000);
        printf("############-->>> Hello YOU  %d, %s\n", c, s);
        x = compyou(s);
        printf("---->> Value = %d\n", x);
    }
}

int main(int argc, char *argv[])
{
 //   localme(10, "cxxxxyyyy");
    localyou(10, "cxxxxxxxx");
    return 0;
}
