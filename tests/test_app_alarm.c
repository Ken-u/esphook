/* Host tests for the framework's alarm registry (framework/core/app_alarm.c): registration, what is due when, rings,
 * snoozes, the one-at-a-time queue and the answers that end a ring. No screen, no storage, no apps. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_alarm.h"
#include "app.h"

/* 2025-11-17 (a Monday) 00:00 in UTC+8, as a UTC epoch. */
#define MONDAY_LOCAL_MIDNIGHT 1763308800U
static app_time_t at(unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    app_time_t t;
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + day * 86400U + hour * 3600U + minute * 60U + second, 480, &t);
    assert(t.valid && (unsigned)t.hour == hour && (unsigned)t.minute == minute && (unsigned)t.second == second);
    return t;
}
static unsigned fire(const app_time_t *t, uint64_t ms, bool idle)
{
    unsigned id = 0;
    return app_alarm_poll(t, ms, idle, &id) ? id : 0U;
}
static app_alarm_spec_t clock_spec(const char *owner, unsigned hour, unsigned minute, unsigned days)
{
    return (app_alarm_spec_t){.owner = owner, .kind = APP_ALARM_CLOCK, .enabled = true, .minute = (uint8_t)minute, .day_mask = (uint8_t)days,
                              .hour_mask = 1U << hour, .max_snoozes = 3};
}
static app_alarm_spec_t after_spec(const char *owner, unsigned seconds)
{
    return (app_alarm_spec_t){.owner = owner, .kind = APP_ALARM_AFTER, .enabled = true, .after_s = seconds};
}
static app_alarm_info_t info(unsigned id)
{
    app_alarm_info_t i;
    assert(app_alarm_get(id, &i));
    return i;
}

static void test_validation(void)
{
    app_alarm_reset();
    app_alarm_spec_t good = clock_spec("clock", 7, 30, APP_ALARM_EVERY_DAY);
    assert(app_alarm_add(&good) == 1U);
    assert(!app_alarm_add(NULL));
    app_alarm_spec_t s;
    s = good; s.owner = NULL; assert(!app_alarm_add(&s));
    s = good; s.owner = ""; assert(!app_alarm_add(&s));
    s = good; s.owner = "Clock"; assert(!app_alarm_add(&s)); /* ids are lower case, digits and underscores, like the catalog's */
    s = good; s.owner = "a/b"; assert(!app_alarm_add(&s));
    s = good; s.owner = "abcdefghijklmnopqrstuvwx"; assert(!app_alarm_add(&s)); /* 24 characters: one too many */
    s = good; s.owner = "abcdefghijklmnopqrstuvw"; assert(app_alarm_add(&s));   /* 23 is the longest id */
    s = good; s.target = "no way"; assert(!app_alarm_add(&s));
    s = good; s.label = "0123456789abcdef"; assert(!app_alarm_add(&s));      /* 16 bytes */
    s = good; s.label = "闹钟闹钟闹"; assert(app_alarm_add(&s));              /* five Chinese characters are exactly 15 bytes */
    s = good; s.kind = (app_alarm_kind_t)0; assert(!app_alarm_add(&s));
    s = good; s.kind = (app_alarm_kind_t)9; assert(!app_alarm_add(&s));
    s = good; s.minute = 60; assert(!app_alarm_add(&s));
    s = good; s.day_mask = 0; assert(!app_alarm_add(&s));
    s = good; s.day_mask = 0x80; assert(!app_alarm_add(&s));
    s = good; s.hour_mask = 0; assert(!app_alarm_add(&s));
    s = good; s.hour_mask = 1U << 24; assert(!app_alarm_add(&s));
    s = good; s.window_s = 3601; assert(!app_alarm_add(&s));
    s = good; s.snooze_s = 3601; assert(!app_alarm_add(&s));
    s = after_spec("game", 0); assert(!app_alarm_add(&s));
    s = after_spec("game", 604801); assert(!app_alarm_add(&s));
    s = after_spec("game", 604800); assert(app_alarm_add(&s));
    s = (app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AT, .epoch = APP_EPOCH_MIN - 1U}; assert(!app_alarm_add(&s));
    s.epoch = APP_EPOCH_MIN; assert(app_alarm_add(&s));
}
static void test_registry(void)
{
    app_alarm_reset();
    unsigned ids[APP_ALARM_SLOTS + 2];
    app_alarm_spec_t s = clock_spec("clock", 7, 30, APP_ALARM_EVERY_DAY);
    for (unsigned k = 0; k < APP_ALARM_SLOTS; ++k) { s.minute = (uint8_t)k; ids[k] = app_alarm_add(&s); assert(ids[k]); }
    assert(!app_alarm_add(&s)); /* the registry is full */
    assert(app_alarm_list(NULL, NULL, 0) == APP_ALARM_SLOTS);
    for (unsigned a = 0; a < APP_ALARM_SLOTS; ++a) for (unsigned b = a + 1; b < APP_ALARM_SLOTS; ++b) assert(ids[a] != ids[b]);
    assert(app_alarm_cancel(ids[3]) && !app_alarm_cancel(ids[3]) && !app_alarm_cancel(0) && !app_alarm_cancel(256));
    assert(app_alarm_add(&s) && !app_alarm_add(&s));
    /* ids roll round and never name an entry that is still in use */
    for (unsigned n = 0; n < 700; ++n) {
        unsigned all[APP_ALARM_SLOTS];
        assert(app_alarm_list(NULL, all, APP_ALARM_SLOTS) == APP_ALARM_SLOTS);
        assert(app_alarm_cancel(all[n % APP_ALARM_SLOTS]));
        const unsigned fresh = app_alarm_add(&s);
        assert(fresh >= 1U && fresh <= 255U);
        assert(app_alarm_list(NULL, all, APP_ALARM_SLOTS) == APP_ALARM_SLOTS);
        for (unsigned x = 0; x < APP_ALARM_SLOTS; ++x) for (unsigned y = x + 1; y < APP_ALARM_SLOTS; ++y) assert(all[x] != all[y]);
    }
    /* owners: listing, replacing and cancelling are by owner */
    app_alarm_reset();
    app_alarm_spec_t a = clock_spec("clock", 7, 0, APP_ALARM_EVERY_DAY), b = after_spec("fishing", 20);
    b.target = "clock"; b.label = "鱼上钩了"; b.tag = 77;
    const unsigned ia = app_alarm_add(&a), ib = app_alarm_add(&b), ic = app_alarm_add(&a);
    assert(app_alarm_list("clock", ids, 8) == 2 && ids[0] == ia && ids[1] == ic && app_alarm_list("fishing", ids, 8) == 1 && ids[0] == ib);
    assert(app_alarm_list("nobody", ids, 8) == 0 && app_alarm_list("clock", NULL, 0) == 2);
    app_alarm_info_t i = info(ia);
    assert(!strcmp(i.owner, "clock") && !strcmp(i.target, "clock") && !i.label[0] && i.window_s == 60 && i.snooze_s == 300 && i.state == APP_ALARM_WAITING);
    i = info(ib);
    assert(!strcmp(i.owner, "fishing") && !strcmp(i.target, "clock") && !strcmp(i.label, "鱼上钩了") && i.tag == 77 && i.id == ib);
    b.after_s = 99;
    assert(!app_alarm_update(ib, &a));                  /* somebody else's entry cannot be rewritten as one's own */
    assert(app_alarm_update(ib, &b) && info(ib).after_s == 99 && !app_alarm_update(250, &b));
    b.after_s = 0;
    assert(!app_alarm_update(ib, &b) && info(ib).after_s == 99); /* an invalid spec leaves the entry as it was */
    assert(app_alarm_cancel_owner("clock") == 2 && app_alarm_list(NULL, NULL, 0) == 1 && app_alarm_cancel_owner("clock") == 0 && !app_alarm_cancel_owner(NULL));
    app_alarm_info_t none;
    assert(!app_alarm_get(ia, &none) && !app_alarm_get(ib, NULL) && app_alarm_get(ib, &none));
    /* a CLOCK entry only starts inside its own minute, so a longer window is cut to a minute */
    app_alarm_spec_t w = clock_spec("clock", 7, 0, APP_ALARM_EVERY_DAY);
    w.window_s = 600;
    assert(info(app_alarm_add(&w)).window_s == 60);
}

static void test_clock_entries(void)
{
    app_alarm_reset();
    const unsigned id = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .target = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 30,
                                                          .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 7, .max_snoozes = 3, .tag = 5});
    assert(fire(&(app_time_t){0}, 0, false) == 0);            /* no clock, no alarm */
    app_time_t t = at(0, 7, 29, 59);
    assert(fire(&t, 100, false) == 0);
    t = at(0, 7, 30, 0);
    assert(fire(&t, 200, false) == id && info(id).state == APP_ALARM_RINGING);
    assert(fire(&t, 300, false) == 0);                       /* one ring at a time, and it is ringing */
    app_alarm_done(id);
    assert(info(id).state == APP_ALARM_WAITING);
    t = at(0, 7, 30, 45);
    assert(fire(&t, 400, false) == 0);                       /* it has rung: the rest of its minute is quiet */
    t = at(0, 7, 31, 0);
    assert(fire(&t, 500, false) == 0);
    t = at(1, 7, 30, 0);
    assert(fire(&t, 600, false) == id);                      /* and it is back tomorrow */
    app_alarm_done(id);
    app_alarm_spec_t off = clock_spec("alarm", 7, 30, APP_ALARM_EVERY_DAY);
    off.target = "alarm"; off.enabled = false;
    assert(app_alarm_update(id, &off));
    t = at(2, 7, 30, 0);
    assert(fire(&t, 700, false) == 0);                       /* switched off */
    /* anywhere in its minute counts: a poll that comes late still starts it */
    app_alarm_spec_t on = off; on.enabled = true;
    assert(app_alarm_update(id, &on));
    t = at(2, 7, 30, 59);
    assert(fire(&t, 800, false) == id);
    app_alarm_done(id);

    /* weekdays only: Friday rings, Saturday and Sunday do not, Monday does again */
    app_alarm_reset();
    app_alarm_spec_t wk = clock_spec("alarm", 6, 0, APP_ALARM_WEEKDAYS);
    const unsigned wid = app_alarm_add(&wk);
    for (unsigned day = 0; day < 8; ++day) {
        t = at(day, 6, 0, 3);
        const unsigned weekday = (1U + day) % 7U;
        assert((unsigned)t.weekday == weekday);
        const unsigned got = fire(&t, 1000 + day, false);
        assert((got == wid) == (weekday >= 1U && weekday <= 5U));
        if (got) app_alarm_done(got);
    }
    /* once: it rings, switches itself off, and stays registered for its owner to switch on again */
    app_alarm_reset();
    app_alarm_spec_t once = clock_spec("alarm", 8, 15, APP_ALARM_EVERY_DAY);
    once.once = true;
    const unsigned oid = app_alarm_add(&once);
    t = at(2, 8, 15, 0);
    assert(fire(&t, 0, false) == oid && !info(oid).enabled);
    app_alarm_done(oid);
    assert(info(oid).state == APP_ALARM_WAITING && fire(&t, 10, false) == 0);
    t = at(3, 8, 15, 0);
    assert(fire(&t, 20, false) == 0);                        /* it is off: tomorrow stays quiet */
    once.enabled = true;
    assert(app_alarm_update(oid, &once));                    /* switched on again for the next time... */
    t = at(2, 8, 15, 30);
    assert(fire(&t, 30, false) == 0);                        /* ...but not for the minute it already rang in */
    t = at(4, 8, 15, 0);
    assert(fire(&t, 40, false) == oid);
    /* a time zone is only a way of writing the same instant: the entry follows the local clock */
    app_alarm_reset();
    const unsigned zid = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 30,
                                                           .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 7});
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 480, &t);
    assert(fire(&t, 0, false) == zid);
    app_alarm_done(zid);
    app_alarm_reset();
    const unsigned zid2 = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 30,
                                                            .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 7});
    app_time_from_epoch(MONDAY_LOCAL_MIDNIGHT + 7 * 3600U + 30 * 60U, 0, &t); /* the same instant, in London */
    assert(t.hour == 23 && fire(&t, 0, false) == 0);
    (void)zid2;
    /* the minutes of the day are told apart, even across midnight */
    app_alarm_reset();
    const unsigned mid = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 59,
                                                           .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 23});
    for (unsigned day = 0; day < 3; ++day) { t = at(day, 23, 59, 0); assert(fire(&t, day, false) == mid); app_alarm_done(mid); }
}

static void test_snooze(void)
{
    app_alarm_reset();
    const unsigned id = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 0,
                                                          .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 7, .max_snoozes = 3, .snooze_s = 300});
    app_time_t t = at(0, 7, 0, 10);
    assert(!app_alarm_snooze(id));                           /* nothing is ringing yet */
    assert(fire(&t, 1000, false) == id);
    assert(app_alarm_snooze(id) && info(id).state == APP_ALARM_SNOOZED && info(id).snoozes == 1);
    assert(fire(&(app_time_t){0}, 300999, false) == 0);      /* the snooze runs on uptime: no wall clock is involved */
    assert(app_alarm_seconds_until(id, NULL, 301000 - 2500) == 3 && app_alarm_seconds_until(id, NULL, 301000) == 0);
    assert(fire(&(app_time_t){0}, 301000, false) == id && info(id).state == APP_ALARM_RINGING && info(id).snoozes == 1); /* the count survives */
    assert(app_alarm_snooze(id) && app_alarm_snooze(id) == false); /* not ringing any more: a second answer to the same ring is refused */
    assert(fire(&(app_time_t){0}, 700000, false) == id);
    assert(app_alarm_snooze(id));                            /* the third and last */
    assert(fire(&(app_time_t){0}, 1100000, false) == id && info(id).snoozes == 3);
    assert(!app_alarm_snooze(id) && info(id).state == APP_ALARM_WAITING && info(id).snoozes == 0); /* out of snoozes: the ring is over */
    /* an entry that cannot be snoozed is simply over */
    app_alarm_spec_t fixed = clock_spec("alarm", 7, 1, APP_ALARM_EVERY_DAY);
    fixed.max_snoozes = 0;
    const unsigned fid = app_alarm_add(&fixed);
    t = at(0, 7, 1, 0);
    assert(fire(&t, 2000000, false) == fid && !app_alarm_snooze(fid) && info(fid).state == APP_ALARM_WAITING);
    /* switching it off while it snoozes ends the snooze; a snooze outlives the entry's own time slot */
    app_alarm_reset();
    app_alarm_spec_t s = clock_spec("alarm", 9, 0, APP_ALARM_EVERY_DAY);
    s.once = true;
    const unsigned sid = app_alarm_add(&s);
    t = at(0, 9, 0, 0);
    assert(fire(&t, 0, false) == sid && !info(sid).enabled && app_alarm_snooze(sid));
    assert(fire(&(app_time_t){0}, 300000, false) == sid); /* a once-alarm that rang keeps its snoozes though it is already off */
    assert(app_alarm_snooze(sid));
    s.enabled = false;
    assert(app_alarm_update(sid, &s) && info(sid).state == APP_ALARM_SNOOZED); /* it was off already: saying so changes nothing */
    s.enabled = true;
    assert(app_alarm_update(sid, &s) && info(sid).state == APP_ALARM_WAITING); /* set again by hand: the old snooze is gone */
    assert(fire(&(app_time_t){0}, 700000, false) == 0);
    app_alarm_reset();
    app_alarm_spec_t daily = clock_spec("alarm", 9, 0, APP_ALARM_EVERY_DAY);
    const unsigned did = app_alarm_add(&daily);
    t = at(0, 9, 0, 0);
    assert(fire(&t, 0, false) == did && app_alarm_snooze(did) && info(did).state == APP_ALARM_SNOOZED);
    daily.enabled = false;
    assert(app_alarm_update(did, &daily) && info(did).state == APP_ALARM_WAITING); /* switched off while it snoozes: no ring later */
    assert(fire(&(app_time_t){0}, 400000, false) == 0);
    /* a snooze that is still pending when the entry's next time comes does not stop it */
    app_alarm_reset();
    const unsigned nid = app_alarm_add(&(app_alarm_spec_t){.owner = "alarm", .kind = APP_ALARM_CLOCK, .enabled = true, .minute = 3,
                                                           .day_mask = APP_ALARM_EVERY_DAY, .hour_mask = 1U << 7, .max_snoozes = 3});
    app_alarm_spec_t early = clock_spec("alarm", 7, 0, APP_ALARM_EVERY_DAY);
    const unsigned eid = app_alarm_add(&early);
    t = at(0, 7, 0, 5);
    assert(fire(&t, 0, false) == eid && app_alarm_snooze(eid));
    t = at(0, 7, 3, 0);
    assert(fire(&t, 180000, false) == nid);
}

static void test_update_in_flight(void)
{
    /* a once-alarm: the framework switches it off when it rings, the owner mirrors that in its settings and pushes them */
    app_alarm_reset();
    app_alarm_spec_t c = clock_spec("alarm", 7, 0, APP_ALARM_EVERY_DAY);
    c.once = true;
    const unsigned id = app_alarm_add(&c);
    app_time_t t = at(0, 7, 0, 0);
    assert(fire(&t, 100, false) == id && !info(id).enabled);
    c.enabled = false;
    assert(app_alarm_update(id, &c) && info(id).state == APP_ALARM_RINGING);   /* the owner agrees with the framework: the ring goes on */
    assert(app_alarm_snooze(id) && info(id).state == APP_ALARM_SNOOZED);
    assert(app_alarm_update(id, &c) && info(id).state == APP_ALARM_SNOOZED && info(id).snoozes == 1); /* saying it again leaves the snooze */
    c.minute = 5;
    assert(app_alarm_update(id, &c) && info(id).state == APP_ALARM_WAITING && info(id).snoozes == 0); /* a real edit ends it */
    /* an AFTER timer updated with the same spec keeps its due time; a changed one starts counting again */
    app_alarm_reset();
    assert(!fire(NULL, 1000, false));
    app_alarm_spec_t a = after_spec("game", 10);
    const unsigned aid = app_alarm_add(&a);
    assert(!fire(NULL, 5000, false) && app_alarm_update(aid, &a) && app_alarm_seconds_until(aid, NULL, 5000) == 6);
    a.after_s = 20;
    assert(app_alarm_update(aid, &a) && app_alarm_seconds_until(aid, NULL, 5000) == 20);
}

static void test_chime(void)
{
    app_alarm_reset();
    app_alarm_spec_t c = clock_spec("alarm", 0, 0, APP_ALARM_EVERY_DAY);
    c.hour_mask = 0; for (unsigned h = 8; h <= 22; ++h) c.hour_mask |= 1U << h;
    c.idle_only = true; c.window_s = 15; c.max_snoozes = 0;
    const unsigned id = app_alarm_add(&c);
    app_time_t t = at(0, 9, 0, 0);
    assert(fire(&t, 0, false) == 0);                          /* not on the standby screen: the hour passes in silence */
    t = at(0, 9, 0, 14);
    assert(fire(&t, 100, true) == id);                        /* idle, and still inside the first seconds */
    assert(!app_alarm_snooze(id) && info(id).state == APP_ALARM_WAITING);
    t = at(0, 9, 0, 14);
    assert(fire(&t, 200, true) == 0);                         /* once per hour */
    t = at(0, 10, 0, 15);
    assert(fire(&t, 300, true) == 0);                         /* opening the app late is not made up for */
    t = at(0, 10, 1, 0);
    assert(fire(&t, 400, true) == 0);
    t = at(0, 10, 0, 1);
    assert(fire(&t, 500, true) == id);                        /* the same hour, in time */
    app_alarm_done(id);
    t = at(0, 7, 0, 1);
    assert(fire(&t, 600, true) == 0);                         /* the night is quiet */
    t = at(0, 23, 0, 1);
    assert(fire(&t, 700, true) == 0);
    t = at(0, 22, 0, 1);
    assert(fire(&t, 800, true) == id);
    app_alarm_done(id);
    /* an alarm on the hour has the screen first; its owner may keep the chime quiet behind it */
    app_alarm_spec_t a = clock_spec("alarm", 12, 0, APP_ALARM_EVERY_DAY);
    app_alarm_spec_t all = c; all.hour_mask = APP_ALARM_EVERY_HOUR;
    assert(app_alarm_update(id, &all));
    const unsigned aid = app_alarm_add(&a);
    t = at(1, 12, 0, 2);
    assert(fire(&t, 1000, true) == aid);                      /* not the chime, though both are due */
    assert(fire(&t, 1100, true) == 0);                        /* the chime waits behind the ring */
    app_alarm_done(aid);
    t = at(1, 12, 0, 5);
    assert(fire(&t, 2000, true) == id); /* answered within the chime's seconds: it rings after all */
    app_alarm_done(id);
    app_alarm_reset();
    const unsigned id2 = app_alarm_add(&all), aid2 = app_alarm_add(&a);
    t = at(1, 12, 0, 2);
    assert(fire(&t, 3000, true) == aid2 && app_alarm_skip(id2));
    app_alarm_done(aid2);
    t = at(1, 12, 0, 5);
    assert(fire(&t, 3100, true) == 0);                        /* skipped: this hour has its alarm and nothing else */
    t = at(1, 13, 0, 1);
    assert(fire(&t, 4000, true) == id2);                      /* the next hour is not affected */
    assert(!app_alarm_skip(aid2 + 100) && !app_alarm_skip(0));
    app_alarm_done(id2);
    app_alarm_reset();
    const unsigned id3 = app_alarm_add(&all);
    assert(!app_alarm_skip(id3));                             /* nothing has been polled: there is no clock to skip by */
    app_alarm_stats_t st;
    app_alarm_reset();
    app_alarm_stats(&st);
    assert(st.fired == 0 && st.missed == 0 && st.timeouts == 0);
}

static void test_queue_and_timeouts(void)
{
    /* two entries that start in the same instant: the second waits and starts if it is still in its window */
    app_alarm_reset();
    app_alarm_spec_t x = after_spec("game", 10), y = after_spec("game", 10);
    x.label = "first"; y.label = "second";
    const unsigned ix = app_alarm_add(&x), iy = app_alarm_add(&y);
    assert(!fire(NULL, 5000, false));
    assert(fire(NULL, 10000, false) == ix && !fire(NULL, 10250, false));
    assert(info(iy).state == APP_ALARM_WAITING);
    app_alarm_done(ix);
    assert(!app_alarm_get(ix, &(app_alarm_info_t){0}));        /* a one-shot disappears when answered */
    assert(fire(NULL, 20000, false) == iy);                    /* 10 s late is inside its minute */
    app_alarm_done(iy);
    assert(app_alarm_list(NULL, NULL, 0) == 0);
    /* answered too late for the one behind it: that one is missed, once, and never comes back */
    const unsigned ja = app_alarm_add(&x), jb = app_alarm_add(&y); /* both due 10 s after the poll at 20000 */
    app_alarm_stats_t before, after;
    app_alarm_stats(&before);
    assert(fire(NULL, 30000, false) == ja);
    assert(!fire(NULL, 30000 + 60001, false));                 /* a minute and a moment later the second is out of its window */
    app_alarm_stats(&after);
    assert(after.missed == before.missed + 1 && !app_alarm_get(jb, &(app_alarm_info_t){0}));
    app_alarm_done(ja);
    /* a ring nobody answers is closed by the framework, and the queue moves on */
    app_alarm_reset();
    app_alarm_spec_t c = clock_spec("alarm", 7, 0, APP_ALARM_EVERY_DAY);
    const unsigned cid = app_alarm_add(&c);
    app_alarm_spec_t once = after_spec("game", 5);
    once.window_s = 3600;
    const unsigned oid = app_alarm_add(&once);
    app_time_t t = at(0, 7, 0, 1);
    assert(fire(&t, 1000, false) == cid);
    assert(!fire(&t, 6000, false));                            /* the one-shot is due but must wait */
    assert(!fire(&t, 1000 + APP_ALARM_RING_MAX_MS - 1, false));
    app_alarm_stats_t s0, s1;
    app_alarm_stats(&s0);
    t = at(0, 7, 5, 0);
    assert(fire(&t, 1000 + APP_ALARM_RING_MAX_MS, false) == oid); /* the first ring was closed, the next one starts */
    app_alarm_stats(&s1);
    assert(s1.timeouts == s0.timeouts + 1 && info(cid).state == APP_ALARM_WAITING);
    app_alarm_done(oid);
    /* an unanswered one-shot is gone, not kept for ever */
    const unsigned lone = app_alarm_add(&once);
    t = at(0, 8, 0, 0);
    assert(fire(&t, 130000, false) == lone);
    assert(!fire(&t, 130000 + APP_ALARM_RING_MAX_MS, false) && !app_alarm_get(lone, &(app_alarm_info_t){0}));
}

static void test_leaving(void)
{
    app_alarm_reset();
    app_alarm_spec_t a = after_spec("game", 1);
    a.target = "clock";
    const unsigned ia = app_alarm_add(&a);
    app_alarm_spec_t c = clock_spec("clock", 7, 0, APP_ALARM_EVERY_DAY);
    const unsigned ic = app_alarm_add(&c);
    assert(!fire(NULL, 0, false) && fire(NULL, 1000, false) == ia);
    app_alarm_leaving("game");                                 /* the owner left: the ring is for the target */
    assert(info(ia).state == APP_ALARM_RINGING);
    app_alarm_leaving("clock");
    assert(!app_alarm_get(ia, &(app_alarm_info_t){0}));        /* the target left: counts as answered, the one-shot is gone */
    app_time_t t = at(0, 7, 0, 0);
    assert(fire(&t, 2000, false) == ic);
    app_alarm_leaving(NULL);
    assert(info(ic).state == APP_ALARM_RINGING);
    app_alarm_leaving("clock");
    assert(info(ic).state == APP_ALARM_WAITING && !fire(&t, 3000, false)); /* answered: this minute stays quiet */
}

static void test_one_shots(void)
{
    /* "in N seconds" works without a clock and counts from the latest poll */
    app_alarm_reset();
    assert(!fire(NULL, 10000, false));
    const unsigned id = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AFTER, .enabled = true, .after_s = 30, .tag = 9});
    assert(!fire(NULL, 39999, false) && fire(NULL, 40000, false) == id && !info(id).enabled);
    app_alarm_done(id);
    assert(!app_alarm_get(id, &(app_alarm_info_t){0}));
    /* too late is missed */
    const unsigned late = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AFTER, .enabled = true, .after_s = 5, .window_s = 10});
    app_alarm_stats_t before, after;
    app_alarm_stats(&before);
    assert(!fire(NULL, 41000, false));                         /* due at 45000, window 10 s */
    assert(!fire(NULL, 55001, false) && !app_alarm_get(late, &(app_alarm_info_t){0}));
    app_alarm_stats(&after);
    assert(after.missed == before.missed + 1);
    /* only for the standby screen: it waits for it inside its window, and is missed otherwise */
    app_alarm_reset();
    assert(!fire(NULL, 0, false));
    app_alarm_spec_t idle = after_spec("game", 5);
    idle.idle_only = true; idle.window_s = 20;
    const unsigned w = app_alarm_add(&idle);                   /* due at 5000, may start until 25000 */
    assert(!fire(NULL, 4000, true));
    assert(!fire(NULL, 6000, false) && !fire(NULL, 20000, false));
    assert(fire(NULL, 21000, true) == w);
    app_alarm_done(w);
    const unsigned w2 = app_alarm_add(&idle);                  /* due at 26000, may start until 46000 */
    assert(!fire(NULL, 27000, false));
    assert(!fire(NULL, 47001, true) && !app_alarm_get(w2, &(app_alarm_info_t){0}));
    /* at an instant: needs the clock, and is missed when the clock arrives too late */
    app_alarm_reset();
    const app_time_t target = at(0, 8, 0, 0);
    const unsigned at_id = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AT, .enabled = true, .epoch = target.epoch});
    app_time_t t = at(0, 7, 59, 59);
    assert(!fire(&t, 0, false) && !fire(&(app_time_t){0}, 100, false));
    t = at(0, 8, 0, 0);
    assert(fire(&t, 200, false) == at_id);
    app_alarm_done(at_id);
    assert(app_alarm_list(NULL, NULL, 0) == 0);
    const unsigned late_at = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AT, .enabled = true, .epoch = target.epoch});
    t = at(0, 8, 1, 1);
    assert(!fire(&t, 300, false) && !app_alarm_get(late_at, &(app_alarm_info_t){0}));
}

static void test_until(void)
{
    app_alarm_reset();
    app_alarm_spec_t c = clock_spec("alarm", 7, 30, APP_ALARM_EVERY_DAY);
    const unsigned id = app_alarm_add(&c);
    app_time_t t = at(0, 7, 0, 0);
    assert(app_alarm_seconds_until(id, &t, 0) == 30 * 60);
    t = at(0, 7, 29, 59);
    assert(app_alarm_seconds_until(id, &t, 0) == 1);
    t = at(0, 7, 30, 0);
    assert(app_alarm_seconds_until(id, &t, 0) == 86400);       /* it is due right now: the next one is tomorrow's */
    t = at(0, 8, 0, 0);
    assert(app_alarm_seconds_until(id, &t, 0) == (23 * 60 + 30) * 60);
    app_alarm_spec_t wk = c;
    wk.day_mask = APP_ALARM_WEEKDAYS;
    assert(app_alarm_update(id, &wk));
    t = at(4, 8, 0, 0);                                        /* Friday morning, after the alarm: next is Monday */
    assert(t.weekday == 5 && app_alarm_seconds_until(id, &t, 0) == (3 * 1440 - 30) * 60);
    app_alarm_spec_t sun = c;
    sun.day_mask = 1U << 1;                                    /* Mondays only */
    assert(app_alarm_update(id, &sun));
    t = at(0, 7, 30, 0);
    assert(app_alarm_seconds_until(id, &t, 0) == 7 * 86400);   /* the same weekday next week is eight days of scanning away */
    app_alarm_spec_t off = c;
    off.enabled = false;
    assert(app_alarm_update(id, &off));
    assert(app_alarm_seconds_until(id, &t, 0) == -1);
    assert(app_alarm_seconds_until(999, &t, 0) == -1);
    assert(app_alarm_update(id, &c));
    assert(app_alarm_seconds_until(id, NULL, 0) == -1 && app_alarm_seconds_until(id, &(app_time_t){0}, 0) == -1); /* no clock */
    /* one-shots */
    app_alarm_reset();
    assert(!fire(NULL, 1000, false));
    const unsigned after = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AFTER, .enabled = true, .after_s = 90});
    assert(app_alarm_seconds_until(after, NULL, 1000) == 90 && app_alarm_seconds_until(after, NULL, 1000 + 89001) == 1 &&
           app_alarm_seconds_until(after, NULL, 1000 + 90000) == 0 && app_alarm_seconds_until(after, NULL, 1000 + 95000) == 0);
    const app_time_t base = at(0, 9, 0, 0);
    const unsigned atid = app_alarm_add(&(app_alarm_spec_t){.owner = "game", .kind = APP_ALARM_AT, .enabled = true, .epoch = base.epoch + 100});
    assert(app_alarm_seconds_until(atid, &base, 0) == 100 && app_alarm_seconds_until(atid, NULL, 0) == -1);
    t = at(0, 9, 5, 0);
    assert(app_alarm_seconds_until(atid, &t, 0) == 0);
    assert(fire(&base, 1000 + 90000, false) == after && app_alarm_seconds_until(after, NULL, 0) == 0); /* ringing */
}

int main(void)
{
    test_validation();
    test_registry();
    test_clock_entries();
    test_snooze();
    test_update_in_flight();
    test_chime();
    test_queue_and_timeouts();
    test_leaving();
    test_one_shots();
    test_until();
    puts("alarm registry: validation, slots and owners, clock entries, snooze, idle-only chime, queue and timeouts, one-shots, next-due distance passed");
    return 0;
}
