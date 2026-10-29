#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "interpolation.h"

static void expect_value(interpolation_history *history, uint32_t time, int32_t expected)
{
    int32_t value = 123456;
    assert(interpolation_at(history, time, &value));
    assert(value == expected);
}

int main(void)
{
    interpolation_history history;
    int32_t value;
    interpolation_clear(&history);
    assert(!interpolation_at(&history, 100, &value));
    interpolation_push(&history, 100, 100);
    assert(!interpolation_at(&history, 100, &value));
    interpolation_push(&history, 200, 300);
    expect_value(&history, 100, 100);
    expect_value(&history, 125, 150);
    expect_value(&history, 150, 200);
    expect_value(&history, 200, 300);
    assert(!interpolation_at(&history, 99, &value));
    assert(!interpolation_at(&history, 201, &value));

    interpolation_clear(&history);
    interpolation_push(&history, 0, 300);
    interpolation_push(&history, 100, -100);
    expect_value(&history, 25, 200);
    expect_value(&history, 75, 0);

    interpolation_clear(&history);
    interpolation_push(&history, 0, -8388608);
    interpolation_push(&history, 1000, 8388607);
    expect_value(&history, 0, -8388608);
    expect_value(&history, 500, -1);
    expect_value(&history, 1000, 8388607);
    for (uint32_t time = 0; time <= 1000; ++time) {
        assert(interpolation_at(&history, time, &value));
        long long expected = -8388608LL + 16777215LL * time / 1000;
        assert(value == expected);
    }

    interpolation_clear(&history);
    interpolation_push(&history, UINT32_MAX - 49u, -100);
    interpolation_push(&history, 50u, 300);
    expect_value(&history, 0u, 100);
    expect_value(&history, 50u, 300);
    assert(!interpolation_at(&history, 51u, &value));

    interpolation_clear(&history);
    interpolation_push(&history, 0, 1);
    interpolation_push(&history, 0, 2);
    assert(!interpolation_at(&history, 0, &value));
    interpolation_push(&history, 1001, 3);
    assert(!interpolation_at(&history, 500, &value));

    interpolation_clear(&history);
    for (uint32_t i = 0; i < 40; ++i) {
        interpolation_push(&history, i * 100, (int32_t)i * 1000);
    }
    assert(history.length == INTERPOLATION_CAPACITY);
    assert(!interpolation_at(&history, 700, &value));
    expect_value(&history, 850, 8500);
    expect_value(&history, 3850, 38500);
    assert(!interpolation_at(&history, 3901, &value));
    interpolation_clear(&history);
    assert(!interpolation_at(&history, 3850, &value));

    puts("PASS: interpolation ramps, signed full range, bounds, timeout gaps, timer wrap, and history rotation");
    return 0;
}
