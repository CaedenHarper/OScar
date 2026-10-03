#pragma once

/** Copy the contents of one regular file to another, returning nonzero on success. */
int copy_file(const char* source, const char* destination);

/** Write a regular file to standard output, returning nonzero on success. */
int cat_file(const char* path);
