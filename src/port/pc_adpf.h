/* Android performance hints: see pc_adpf.c. No-ops elsewhere. */
#ifndef PC_ADPF_H
#define PC_ADPF_H

/* The calling thread is one of the frame's: call once per frame from each. */
void pc_adpf_here(void);
/* A frame took this long; call once per present, from any registered thread. */
void pc_adpf_report(long long actual_ns);

#endif
