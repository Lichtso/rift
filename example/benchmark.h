#define SAMPLES 0x40000000UL

uint64_t prng() {
    static uint64_t seed = 0;
    seed = seed * 6364136223846793005 + 1442695040888963407;
    return seed;
}

EXPORT void benchmark_linear_memory_access_pattern() {
    for(uint64_t sample = 0; sample < SAMPLES; ++sample)
        empty_pages[sample % used_memory] += 1;
    EXIT
}

EXPORT void benchmark_random_memory_access_pattern() {
    for(uint64_t sample = 0; sample < SAMPLES; ++sample)
        empty_pages[prng() % used_memory] += 1;
    EXIT
}
