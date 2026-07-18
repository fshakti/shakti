// Java subset examples runnable by `./shakti example.java`.

public class Example {
    static int Square(int x) {
        return x * x;
    }

    static int Clamp(int x, int low, int high) {
        if (x < low) {
            return low;
        } else if (x > high) {
            return high;
        }
        return x;
    }

    public static void main(String[] args) {
        // Arithmetic and static helpers.
        int squared = Square(6);
        System.out.println("square " + squared);
        int clamped = Clamp(15, 0, 12);
        System.out.println("clamp " + clamped);

        // Arrays, maps, loops, and augmented assignment.
        int[] nums = new int[]{2, 4, 6, 8};
        int size = nums.length;
        System.out.println("size " + size);

        var info = Map.of("name", "shakti", "count", size);
        System.out.println("dict " + info.get("name") + " " + info.get("count"));

        int total = 0;
        for (int i = 0; i < nums.length; i++) {
            total += nums[i];
        }
        System.out.println("for " + total);

        for (int n : nums) {
            total += n;
        }
        System.out.println("foreach " + total);

        int countdown = 3;
        while (countdown > 0) {
            System.out.println("while " + countdown);
            countdown -= 1;
        }

        // One-argument lambda and Math helpers.
        var increment = (int x) -> x + 1;
        System.out.println("lambda " + increment(9));
        System.out.println("max " + Math.max(3, 9));

        if (args.length > 0) {
            System.out.println("arg " + args[0]);
        }
    }
}
