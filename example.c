// C subset examples runnable by `./shakti example.c`.

#include <stdio.h>

int Square(int x) {
    return x * x;
}

int Clamp(int x, int low, int high) {
    if (x < low) {
        return low;
    } else if (x > high) {
        return high;
    }
    return x;
}

int main(int argc, char **argv) {
    // Arithmetic and helpers. String + value uses str() lowering.
    int squared = Square(6);
    printf("square " + squared);
    int clamped = Clamp(15, 0, 12);
    printf("clamp " + clamped);

    // Arrays, loops, and augmented assignment.
    int nums[] = {2, 4, 6, 8};
    int size = 4;
    printf("size " + size);

    int total = 0;
    for (int i = 0; i < size; i++) {
        total += nums[i];
    }
    printf("for " + total);

    int countdown = 3;
    while (countdown > 0) {
        printf("while " + countdown);
        countdown -= 1;
    }

    if (argc > 0) {
        printf("arg " + argv[0]);
    }
    return 0;
}
