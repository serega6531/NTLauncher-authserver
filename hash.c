#include "settings.h"
#include "hash.h"
#include <stdio.h>
#include <string.h>

#ifndef HASH_ALGO
#error HASH_ALGO not defined
#endif

#if HASH_ALGO == HASH_SHA256 || HASH_ALGO == HASH_SHA1 || HASH_ALGO == HASH_SHA384 || HASH_ALGO == HASH_SHA512
#include "openssl/sha.h"
#endif

#if HASH_ALGO == HASH_MD5
#include <openssl/md5.h>

void hash(char *string, char *result) {
	unsigned char hash[MD5_DIGEST_LENGTH];
	int i = 0;
	MD5_CTX handler;

	MD5_Init(&handler);
	MD5_Update(&handler, string, strlen(string));
	MD5_Final(hash, &handler);
	memset(result, '\0', strlen(result));
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(result, "%s%02x", result, hash[i]);
	};
}

#endif

#if HASH_ALGO == HASH_SHA1

void hash(char *string, char *result) {
	unsigned char hash[SHA_DIGEST_LENGTH];
	int i = 0;
	SHA_CTX handler;

	SHA1_Init(&handler);
	SHA1_Update(&handler, string, strlen(string));
	SHA1_Final(hash, &handler);
	memset(result, '\0', strlen(result));
	for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
		sprintf(result, "%s%02x", result, hash[i]);
	};
}

#endif

#if HASH_ALGO == HASH_SHA256

void hash(char *string, char *result) {
	unsigned char hash[SHA256_DIGEST_LENGTH];
	int i = 0;
	SHA256_CTX handler;

	SHA256_Init(&handler);
	SHA256_Update(&handler, string, strlen(string));
	SHA256_Final(hash, &handler);
	memset(result, '\0', strlen(result));
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		sprintf(result, "%s%02x", result, hash[i]);
	};
}

#endif

#if HASH_ALGO == HASH_SHA384

void hash(char *string, char *result) {
	unsigned char hash[SHA384_DIGEST_LENGTH];
	int i = 0;
	SHA384_CTX handler;

	SHA384_Init(&handler);
	SHA384_Update(&handler, string, strlen(string));
	SHA384_Final(hash, &handler);
	memset(result, '\0', strlen(result));
	for (i = 0; i < SHA384_DIGEST_LENGTH; i++) {
		sprintf(result, "%s%02x", result, hash[i]);
	};
}

#endif

#if HASH_ALGO == HASH_SHA512

void hash(char *string, char *result) {
	unsigned char hash[SHA512_DIGEST_LENGTH];
	int i = 0;
	SHA512_CTX handler;

	SHA512_Init(&handler);
	SHA512_Update(&handler, string, strlen(string));
	SHA512_Final(hash, &handler);
	memset(result, '\0', strlen(result));
	for (i = 0; i < SHA512_DIGEST_LENGTH; i++) {
		sprintf(result, "%s%02x", result, hash[i]);
	};
}

#endif
