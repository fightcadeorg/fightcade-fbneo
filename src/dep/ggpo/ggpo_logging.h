#ifndef GGPO_LOGGING_H__
#define GGPO_LOGGING_H__

#ifndef GGPO_LOG_TO_FILE
#define GGPO_LOG_TO_FILE 0
#endif

#ifndef GGPO_LOG_ENABLE
#define GGPO_LOG_ENABLE 1
#endif

#include <stdbool.h>

__attribute__((__format__ (__printf__, 1, 2)))
void gglog(char const* format, ...);
void ggindent(void);
void ggdedent(void);
extern int ggpo_logging_indentation;
extern bool ggpo_logging_force;

#endif // GGPO_LOGGING_H__
