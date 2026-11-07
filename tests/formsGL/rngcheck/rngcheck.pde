// Reference values of Processing's random streams for the C++ port (Forms GL Box)
void setup() {
  size(64, 64);
  PrintWriter o = createWriter(sketchPath("rng_java.txt"));
  int[] seeds = {0, 1, 12345, 839201, 2147483000};
  for (int s : seeds) {
    randomSeed(s);
    for (int i = 0; i < 300; i++) o.println("r1 " + s + " " + Float.floatToIntBits(random(1)));
    for (int i = 0; i < 300; i++) o.println("r2 " + s + " " + Float.floatToIntBits(random(-3.5, 17.25)));
    for (int i = 0; i < 100; i++) o.println("ri " + s + " " + int(random(7)));
    for (int i = 0; i < 200; i++) o.println("rg " + s + " " + Float.floatToIntBits(randomGaussian()));
    noiseSeed(s);
    noiseDetail(4, 0.5);
    for (int i = 0; i < 200; i++) o.println("n1 " + s + " " + Float.floatToIntBits(noise(i * 0.137)));
    for (int i = 0; i < 200; i++) o.println("n2 " + s + " " + Float.floatToIntBits(noise(i * 0.071, i * 0.33 + 5.5)));
    noiseDetail(3, 0.65);
    for (int i = 0; i < 100; i++) o.println("n3 " + s + " " + Float.floatToIntBits(noise(i * 0.21, 3.3, i * 0.05)));
    PRNG pr = new PRNG(s);
    for (int i = 0; i < 300; i++) o.println("pr " + s + " " + Float.floatToIntBits(pr.random(0, 1)));
    for (int i = 0; i < 300; i++) o.println("pp " + s + " " + Float.floatToIntBits(pr.perlin(i * 0.173, i * 0.029 + 1.7)));
    for (int i = 0; i < 50; i++) o.println("pm " + s + " " + Float.floatToIntBits(pr.mixPerlin(i * 0.1, 2.2, 4, 2, 0.5)));
    o.println("jm " + s + " " + new java.util.Random(s).nextInt(1 << 30));
  }
  o.flush(); o.close();
  exit();
}
