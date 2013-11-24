#ifndef LOCK_H_
#define LOCK_H_ 1

#include <X11/X.h>

void LOCK_init(void);
void LOCK_handle_key(KeySym symbol, const char* text);
void LOCK_process_frame(float width, float height, double delta_time);

#endif /* !LOCK_H_ */
