#include "ggpo_logging.h"
#include <assert.h>
#include <stdlib.h>

bool ggpo_logging_force = false;
int ggpo_logging_indentation = 0;
void ggindent(void)
{
	ggpo_logging_indentation++;
}

void ggdedent(void)
{
	assert(ggpo_logging_indentation > 0);
	ggpo_logging_indentation--;
}

static bool is_enabled(void)
{
	static const char* env = NULL;
	static bool got_env = false;
	if (!got_env) {
		got_env = true;
		env = getenv("quark.log");
	}

	return env || ggpo_logging_force;
}

#if GGPO_LOG_ENABLE && GGPO_LOG_TO_FILE
// Log to file.

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

void gglog(char const* format, ...)
{
	assert(format);
	if (!is_enabled()) {
		return;
	}

	static FILE* file = NULL;
	if (!file) {
		char filename[255];
		int chars_printed = snprintf(filename, sizeof(filename), "c:\\users\\ponder\\log_api_%d.log", getpid());
		assert(chars_printed < sizeof(filename));
		file = fopen(filename, "wb");
		assert(file);
	}
	for (int i = 0; i < ggpo_logging_indentation; i++) {
		fprintf(file, "    ");
	}
	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);

	fflush(file);
}

#elif GGPO_LOG_ENABLE
// Log to stdout.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void gglog(char const* format, ...)
{
	assert(format);
	if (!is_enabled()) {
		return;
	}

	for (int i = 0; i < ggpo_logging_indentation; i++) {
		printf("    ");
	}
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	size_t length = strlen(format);
	if (length > 0 && format[length - 1] == '\n') {
		fflush(stdout);
	}
}

#else
// Disable all logging.

void gglog(char const* format, ...)
{
	(void)format;
}

#endif