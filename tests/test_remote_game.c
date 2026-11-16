#include "remote_game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool apply(remote_game_t *g, const char *j, uint64_t now)
{
    char error[96]; int sequence;
    return remote_game_apply(g, j, strlen(j), now, &sequence, error, sizeof error);
}
int main(void)
{
    remote_game_t g = {0};
    const char *begin = "{\"v\":1,\"seq\":1,\"id\":24,\"op\":\"begin\",\"count\":3,\"first\":1,\"code\":\"600000\",\"date\":\"2026-11-12\",\"label\":\"测试\"}";
    const char *show = "{\"v\":1,\"seq\":3,\"id\":24,\"op\":\"show\",\"cursor\":1,\"state\":\"play\",\"held_ms\":500,\"trades\":1,\"pct_bps\":123,\"baseline_bps\":100,\"pnl_cents\":123000,\"holding\":true,\"clock\":\"09:35\"}";
    assert(apply(&g, begin, 0) && !g.active);
    assert(!apply(&g, show, 0));
    assert(apply(&g, "{\"v\":1,\"seq\":2,\"id\":24,\"op\":\"bars\",\"offset\":0,\"bars\":[[100,120,90,110,105,106],[110,130,100,120,115,116],[120,140,110,130,125,126]]}", 0));
    assert(apply(&g, show, 100) && g.active);
    remote_game_view_t view;
    remote_game_snapshot(&g, 100, &view);
    assert(view.count == 2 && view.bars[1].close == 120); /* Future third candle is hidden. */
    assert(view.held_ms == 500 && view.holding && view.pct_bps == 123);
    uint16_t pixels[160 * 48];
    stock_chart_draw(pixels, &view);
    assert(pixels[47 * 160 + 159] == 0xFFE0);
    remote_game_snapshot(&g, 5101, &view); assert(view.offline);
    assert(apply(&g, "{\"v\":1,\"seq\":4,\"id\":24,\"op\":\"heartbeat\"}", 5200));
    remote_game_snapshot(&g, 5200, &view); assert(!view.offline);
    remote_game_t before = g;
    const char *bad[] = {
        "{\"v\":2,\"seq\":5,\"id\":24,\"op\":\"close\"}",
        "{\"v\":1,\"seq\":5,\"id\":25,\"op\":\"close\"}",
        "{\"v\":1,\"seq\":5,\"seq\":6,\"id\":24,\"op\":\"close\"}",
        "{\"v\":1,\"seq\":5,\"id\":24,\"op\":\"close\"} trailing",
        "{\"v\":1,\"seq\":5,\"id\":24,\"op\":\"bars\",\"offset\":0,\"bars\":[[100,90,110,100,100,100]]}",
        "{\"v\":1,\"seq\":5,\"id\":24,\"op\":\"bars\",\"offset\":128,\"bars\":[[100,120,90,110,105,106]]}",
        "{\"v\":1,\"seq\":5,\"id\":24,\"op\":\"begin\",\"count\":3,\"first\":1,\"code\":\"600000\",\"date\":\"2026-11-12\",\"label\":\"reuse\"}",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(!apply(&g, bad[i], 5300)); assert(!memcmp(&before, &g, sizeof g));
    }
    assert(apply(&g, "{\"v\":1,\"seq\":6,\"id\":24,\"op\":\"close\"}", 5400) && !g.active);
    assert(!apply(&g, show, 5500)); /* Closing discards staged data too. */
    assert(apply(&g, begin, 6000));
    /* Reversed, repeated chunks are safe; one invalid row leaves an entire chunk untouched. */
    assert(apply(&g, "{\"v\":1,\"seq\":7,\"id\":24,\"op\":\"bars\",\"offset\":2,\"bars\":[[100,100,100,100,100,100]]}", 6000));
    before = g;
    assert(!apply(&g, "{\"v\":1,\"seq\":7,\"id\":24,\"op\":\"bars\",\"offset\":0,\"bars\":[[100,100,100,100,100,100],[100,0,100,100,100,100]]}", 6000));
    assert(!memcmp(&before, &g, sizeof g));
    /* Empty and flat charts remain bounded and do not divide by zero. */
    memset(&view, 0, sizeof view); stock_chart_draw(pixels, &view);
    view.count = 48; view.cursor = 60; view.first = 60;
    for (unsigned i = 0; i < 48; ++i) view.bars[i] = (chart_bar_t){100,100,100,100,100,100};
    stock_chart_draw(pixels, &view);
    for (unsigned i = 0; i < 48; ++i) view.bars[i] = (chart_bar_t){100000000,100000000,1,1,100000000,1};
    stock_chart_draw(pixels, &view);
    puts("remote game: atomic upload, complete commit, stale session, hidden future, watchdog and chart bounds passed");
}
