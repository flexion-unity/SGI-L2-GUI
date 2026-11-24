/*
 * demo.h - offline L2 simulator (sgil2gui -demo)
 *
 * Produces text in the same format as a real L2 controller so the
 * whole parse / render path is exercised without hardware.
 */
#ifndef SGIL2_DEMO_H
#define SGIL2_DEMO_H

void  demo_init(void);
/* returns malloc'd response text for an L2 command */
char *demo_exec(const char *cmd);

#endif
