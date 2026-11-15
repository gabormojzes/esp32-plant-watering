// Linked into every test in a sanitized build. LeakSanitizer reads this list
// before the leak check that runs at exit.

// The queue stub never frees a queue, as the device never does. Every other
// allocation still has to be freed. The runtime looks the hook up by this name.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
extern "C" const char* __lsan_default_suppressions() {
    return "leak:xQueueCreate\n";
}
