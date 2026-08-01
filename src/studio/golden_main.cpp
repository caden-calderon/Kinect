// kstudio-golden — writes the synthetic golden take fixture.
//   kstudio-golden <output.mcap> [frames]
#include <cstdio>
#include <cstdlib>

#include "replay/golden.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: kstudio-golden <output.mcap> [frames]\n");
    return 64;
  }
  const int frames = argc > 2 ? std::atoi(argv[2]) : 60;
  if (!kstudio::writeGoldenTake(argv[1], frames)) {
    std::fprintf(stderr, "golden take generation failed\n");
    return 1;
  }
  std::printf("golden take written: %s (%d frames)\n", argv[1], frames);
  return 0;
}
