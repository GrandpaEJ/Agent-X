#ifndef LOGGER_H
#define LOGGER_H

void log_info(const char* event, const char* details);
void log_warn(const char* event, const char* details);
void log_error(const char* event, const char* details);

#endif // LOGGER_H
