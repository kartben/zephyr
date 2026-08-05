/* SPDX-License-Identifier: MIT */

/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * @file
 * @brief HTTP request and response parser, based on the nodejs/http-parser library.
 *
 * @defgroup http_parser HTTP parser API
 * @ingroup networking
 * @{
 */

#ifndef ZEPHYR_INCLUDE_NET_HTTP_PARSER_H_
#define ZEPHYR_INCLUDE_NET_HTTP_PARSER_H_

/* Also update SONAME in the Makefile whenever you change these. */
/** Major version of the http-parser library this parser is based on */
#define HTTP_PARSER_VERSION_MAJOR 2
/** Minor version of the http-parser library this parser is based on */
#define HTTP_PARSER_VERSION_MINOR 7
/** Patch level of the http-parser library this parser is based on */
#define HTTP_PARSER_VERSION_PATCH 1

#include <sys/types.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
	(!defined(_MSC_VER) || _MSC_VER < 1600) && !defined(__WINE__)
#include <BaseTsd.h>
#include <stddef.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <zephyr/types.h>
#include <stddef.h>
#endif
#include <zephyr/net/http/method.h>
#include <zephyr/net/http/parser_state.h>
#include <zephyr/net/http/parser_url.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DHTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef HTTP_MAX_HEADER_SIZE
/** Maximum size of the header section of a message, in bytes */
# define HTTP_MAX_HEADER_SIZE (80 * 1024)
#endif

struct http_parser;
struct http_parser_settings;


/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * Returning `2` from on_headers_complete will tell parser that it should not
 * expect neither a body nor any further responses on this connection. This is
 * useful for handling responses to a CONNECT request which may not contain
 * `Upgrade` or `Connection: upgrade` headers.
 *
 * http_data_cb does not return data chunks. It will be called arbitrarily
 * many times for each string. E.G. you might get 10 callbacks for "on_url"
 * each providing just a few characters more data.
 */
/**
 * @brief Callback delivering a run of message data
 *
 * @param parser Parser the data was read by
 * @param at Start of the data run
 * @param length Length of the data run
 *
 * @return 0 to continue parsing, nonzero to halt it
 */
typedef int (*http_data_cb)(struct http_parser *parser, const char *at,
			    size_t length);

/**
 * @brief Callback notifying that a parsing milestone was reached
 *
 * @param parser Parser that reached the milestone
 *
 * @return 0 to continue parsing, nonzero to halt it
 */
typedef int (*http_cb)(struct http_parser *parser);

/** @brief Kind of message a parser accepts */
enum http_parser_type {
	HTTP_REQUEST,  /**< Requests only */
	HTTP_RESPONSE, /**< Responses only */
	HTTP_BOTH      /**< Requests and responses */
};

/** @brief Flag values for the http_parser.flags field */
enum flags {
	F_CHUNKED               = 1 << 0, /**< Body uses chunked transfer encoding */
	F_CONNECTION_KEEP_ALIVE = 1 << 1, /**< Connection header requests keep-alive */
	F_CONNECTION_CLOSE      = 1 << 2, /**< Connection header requests close */
	F_CONNECTION_UPGRADE    = 1 << 3, /**< Connection header requests an upgrade */
	F_TRAILING              = 1 << 4, /**< Headers being parsed are chunked trailers */
	F_UPGRADE               = 1 << 5, /**< Upgrade header was present */
	F_SKIPBODY              = 1 << 6, /**< No body is expected for this message */
	F_CONTENTLENGTH         = 1 << 7  /**< Content-Length header was present */
};

/** @brief Reason a parser stopped */
enum http_errno {
	HPE_OK,                        /**< Success */
	HPE_CB_message_begin,          /**< The on_message_begin callback failed */
	HPE_CB_url,                    /**< The on_url callback failed */
	HPE_CB_header_field,           /**< The on_header_field callback failed */
	HPE_CB_header_value,           /**< The on_header_value callback failed */
	HPE_CB_headers_complete,       /**< The on_headers_complete callback failed */
	HPE_CB_body,                   /**< The on_body callback failed */
	HPE_CB_message_complete,       /**< The on_message_complete callback failed */
	HPE_CB_status,                 /**< The on_status callback failed */
	HPE_CB_chunk_header,           /**< The on_chunk_header callback failed */
	HPE_CB_chunk_complete,         /**< The on_chunk_complete callback failed */
	HPE_INVALID_EOF_STATE,         /**< Stream ended at an unexpected time */
	HPE_HEADER_OVERFLOW,           /**< Too many header bytes seen */
	HPE_CLOSED_CONNECTION,         /**< Data received after a close message */
	HPE_INVALID_VERSION,           /**< Invalid HTTP version */
	HPE_INVALID_STATUS,            /**< Invalid HTTP status code */
	HPE_INVALID_METHOD,            /**< Invalid HTTP method */
	HPE_INVALID_URL,               /**< Invalid URL */
	HPE_INVALID_HOST,              /**< Invalid host */
	HPE_INVALID_PORT,              /**< Invalid port */
	HPE_INVALID_PATH,              /**< Invalid path */
	HPE_INVALID_QUERY_STRING,      /**< Invalid query string */
	HPE_INVALID_FRAGMENT,          /**< Invalid fragment */
	HPE_LF_EXPECTED,               /**< LF character expected */
	HPE_INVALID_HEADER_TOKEN,      /**< Invalid character in a header */
	HPE_INVALID_CONTENT_LENGTH,    /**< Invalid character in the Content-Length header */
	HPE_UNEXPECTED_CONTENT_LENGTH, /**< Unexpected Content-Length header */
	HPE_INVALID_CHUNK_SIZE,        /**< Invalid character in a chunk size header */
	HPE_INVALID_CONTENT_RANGE,     /**< Invalid character in the Content-Range header */
	HPE_UNEXPECTED_CONTENT_RANGE,  /**< Unexpected Content-Range header */
	HPE_INVALID_CONSTANT,          /**< Invalid constant string */
	HPE_INVALID_INTERNAL_STATE,    /**< Encountered an unexpected internal state */
	HPE_STRICT,                    /**< Strict mode assertion failed */
	HPE_PAUSED,                    /**< Parser is paused */
	HPE_UNKNOWN                    /**< An unknown error occurred */
};

/**
 * @brief Get the reason a parser stopped
 *
 * @param p Parser to read the reason from
 *
 * @return An @ref http_errno value
 */
#define HTTP_PARSER_ERRNO(p)            ((enum http_errno) (p)->http_errno)

/** @brief Parsed value of a Content-Range header field */
struct http_content_range {
	uint64_t start; /**< First byte of the range */
	uint64_t end;   /**< Last byte of the range */
	uint64_t total; /**< Total size of the representation, 0 if not supplied */
};

/** @brief HTTP parser instance */
struct http_parser {
	/** Kind of message being parsed, an @ref http_parser_type value */
	unsigned int type : 2;
	/** Parser flags, F_xxx values from @ref flags */
	unsigned int flags : 8;
	/** @cond INTERNAL_HIDDEN */
	unsigned int state : 7;
	unsigned int header_state : 7;
	unsigned int index : 7;
	unsigned int lenient_http_headers : 1;

	uint32_t nread;
	/** @endcond */
	/**
	 * Number of bytes in the body, 0 if there is no Content-Length header.
	 *
	 * While the on_chunk_header callback runs, this holds the length of
	 * the chunk that is about to be parsed instead.
	 */
	uint64_t content_length;
	/** Was a Content-Range header field present */
	bool content_range_present;
	/** Parsed value of the Content-Range header field */
	struct http_content_range content_range;

	/* READ-ONLY */

	/** Major version of the parsed message */
	unsigned short http_major;
	/** Minor version of the parsed message */
	unsigned short http_minor;
	/** Status code of the parsed message, responses only */
	unsigned int status_code : 16;
	/** Method of the parsed message, an @ref http_method value, requests only */
	unsigned int method : 8;
	/** Reason the parser stopped, an @ref http_errno value */
	unsigned int http_errno : 7;

	/**
	 * Set when an Upgrade header field was present and the parser exited
	 * because of it. Check it whenever http_parser_execute() returns, in
	 * addition to checking for errors.
	 */
	unsigned int upgrade : 1;

	/* PUBLIC */

	/** Free for the caller to point at its connection or socket object */
	void *data;

	/**
	 * Remote socket address of the connection, so that the parser can
	 * initiate replies when it needs to.
	 */
	const struct net_sockaddr *addr;
};


/** @brief Callbacks invoked while a message is parsed */
struct http_parser_settings {
	/** Called when a message starts */
	http_cb      on_message_begin;
	/** Called with the request URL, possibly several times */
	http_data_cb on_url;
	/** Called with the response status text, possibly several times */
	http_data_cb on_status;
	/** Called with a header field name, possibly several times */
	http_data_cb on_header_field;
	/** Called with a header field value, possibly several times */
	http_data_cb on_header_value;
	/**
	 * Called once the header section has been parsed.
	 *
	 * Returning 1 tells a response parser not to expect a body, which is
	 * what a response to a HEAD request needs. Returning 2 tells it to
	 * expect neither a body nor any further response on this connection,
	 * which is what a response to a CONNECT request needs.
	 */
	http_cb      on_headers_complete;
	/** Called with a run of body data, possibly several times */
	http_data_cb on_body;
	/** Called once the whole message has been parsed */
	http_cb      on_message_complete;
	/**
	 * Called when a chunk header has been parsed.
	 *
	 * The length of the chunk is in http_parser.content_length.
	 */
	http_cb      on_chunk_header;
	/** Called once a chunk has been parsed */
	http_cb      on_chunk_complete;
};


/**
 * @brief Get the version of the http-parser library this parser is based on
 *
 * Bits 16-23 hold the major version number, bits 8-15 the minor version number
 * and bits 0-7 the patch level:
 *
 * @code
 * unsigned long version = http_parser_version();
 * unsigned major = (version >> 16) & 255;
 * unsigned minor = (version >> 8) & 255;
 * unsigned patch = version & 255;
 *
 * printf("http_parser v%u.%u.%u\n", major, minor, patch);
 * @endcode
 *
 * @return Packed library version
 */
unsigned long http_parser_version(void);

/**
 * @brief Initialize a parser
 *
 * @param parser Parser to initialize
 * @param type Kind of message the parser accepts
 */
void http_parser_init(struct http_parser *parser, enum http_parser_type type);


/**
 * @brief Initialize all http_parser_settings members to 0
 *
 * @param settings Callbacks to initialize
 */
void http_parser_settings_init(struct http_parser_settings *settings);


/**
 * @brief Run the parser over a buffer
 *
 * Sets http_parser.http_errno when the message cannot be parsed. Read it with
 * @ref HTTP_PARSER_ERRNO.
 *
 * @param parser Parser to run
 * @param settings Callbacks to invoke while parsing
 * @param data Buffer to parse
 * @param len Length of the data in the buffer
 *
 * @return Number of bytes parsed
 */
size_t http_parser_execute(struct http_parser *parser,
			   const struct http_parser_settings *settings,
			   const char *data, size_t len);

/**
 * @brief Test whether the connection can be reused after this message
 *
 * When this returns 0 from the on_headers_complete or on_message_complete
 * callback, the message being parsed is the last one on the connection. A
 * server should then answer with a "Connection: close" header, and a client
 * should close the connection.
 *
 * @param parser Parser that read the message
 *
 * @return Nonzero when the connection can be reused, 0 otherwise
 */
int http_should_keep_alive(const struct http_parser *parser);

/**
 * @brief Get the name of an HTTP method
 *
 * @param m Method to name
 *
 * @return Name of the method
 */
const char *http_method_str(enum http_method m);

/**
 * @brief Get the name of a parser error
 *
 * @param err Error to name
 *
 * @return Name of the error
 */
const char *http_errno_name(enum http_errno err);

/**
 * @brief Get the description of a parser error
 *
 * @param err Error to describe
 *
 * @return Description of the error
 */
const char *http_errno_description(enum http_errno err);

/**
 * @brief Pause or un-pause a parser
 *
 * @param parser Parser to pause or un-pause
 * @param paused Nonzero to pause the parser, 0 to un-pause it
 */
void http_parser_pause(struct http_parser *parser, int paused);

/**
 * @brief Test whether the body chunk being parsed is the final one
 *
 * @param parser Parser reading the body
 *
 * @return Nonzero for the final chunk, 0 otherwise
 */
int http_body_is_final(const struct http_parser *parser);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif
