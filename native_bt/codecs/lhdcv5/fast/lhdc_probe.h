/* Bench-only section timers (LHDC_ENTROPY_PROBE). g_ep_sec[n] accumulates cycles. */
#ifndef LHDC_PROBE_H
#define LHDC_PROBE_H
#include <stdint.h>
#ifdef LHDC_ENTROPY_PROBE
#include "esp_cpu.h"
extern uint32_t g_ep_sec[16];
#define EPM_DECL uint32_t _epm = esp_cpu_get_cycle_count()
#define EPM(n) do { uint32_t _n = esp_cpu_get_cycle_count(); g_ep_sec[n] += _n - _epm; _epm = _n; } while (0)
#else
#define EPM_DECL do { } while (0)
#define EPM(n) do { } while (0)
#endif
#endif
