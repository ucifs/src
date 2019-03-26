/* $OpenBSD: ber_test.c,v 1.2 2019/03/26 23:09:48 rob Exp $
*/
/*
 * Copyright (c) Rob Pierce <rob@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <ber.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BER_TAG_MASK	0x1f

#define SUCCEED	0
#define FAIL	1

struct test_vector {
	bool		 fail;		/* 1 means test is expected to fail */
	char		 title[128];
	size_t		 length;
	unsigned char	 input[1024];
};

struct test_vector test_vectors[] = {
	{
		SUCCEED,
		"boolean",
		3,
		{
			0x01, 0x01, 0xff
		},
	},
	{
		SUCCEED,
		"integer (zero)",
		3,
		{
			0x02, 0x01, 0x00
		},
	},
	{
		SUCCEED,
		"positive integer",
		3,
		{
			0x02, 0x01, 0x63
		},
	},
	{
		SUCCEED,
		"large positive integer",
		5,
		{
			0x02, 0x03, 0x01, 0x00, 0x00
		},
	},
	{
		SUCCEED,
		"negative integer",
		4,
		{
			0x02, 0x02, 0xff, 0x7f
		},
	},
	{
		SUCCEED,
		"bit string",
		6,
		{
			0x03, 0x04, 0xde, 0xad, 0xbe, 0xef
		},
	},
	{
		SUCCEED,
		"octet string",
		10,
		{
			0x04, 0x08, 0x01, 0x23, 0x45,
			0x67, 0x89, 0xab, 0xcd, 0xef
		},
	},
	{
		SUCCEED,
		"null",
		2,
		{
			0x05, 0x00
		},
	},
	{
		SUCCEED,
		"object identifier",
		8,
		{
			0x06, 0x06, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d
		}
	},
	{
		SUCCEED,
		"sequence",	/* ldap */
		14,
		{
			0x30, 0x0c, 0x02, 0x01, 0x01, 0x60, 0x07,
			0x02, 0x01, 0x03, 0x04, 0x00, 0x80, 0x00
		}
	},
	{
		SUCCEED,
		"set with integer and boolean",
		8,
		{
			0x31, 0x06, 0x02, 0x01, 0x04, 0x01, 0x01, 0xff
		}
	},
	{
		FAIL,
		"indefinite encoding (expected failure - unsupported)",
		4,
		{
			0x30, 0x80, 0x00, 0x00
		}
	},
	{
		SUCCEED,
		"maximum long form tagging (i.e. 4 byte tag id)",
		7,
		{
			0x1f, 0x80, 0x80, 0x80, 0x02, 0x01, 0x01
		},
	},
	{
		FAIL,
		"overflow long form tagging (expected failure - unsupported)",
		8,
		{
			0x1f, 0x80, 0x80, 0x80, 0x80, 0x02, 0x01, 0x01
		},
	},
	{
		FAIL,
		"garbage (expected failure - unsupported)",
		4,
		{
			0x99, 0x53, 0x22, 0x66
		}
	}
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t	i;

	for (i = 1; i < len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "": "\n");

	fprintf(stderr, " 0x%02hhx", buf[i - 1]);
	fprintf(stderr, "\n");
}

static int
test(int i)
{
	int			 pos, b;
	char			*string;
	void			*p = NULL;
	ssize_t			 len = 0;
	struct ber_element	*elm = NULL, *ptr = NULL;
	struct ber		 ber;
	long long		 val;
	void			*bstring = NULL;
	struct ber_oid		 oid;
	struct ber_octetstring	 ostring;

	bzero(&ber, sizeof(ber));
	ber_set_readbuf(&ber, test_vectors[i].input, test_vectors[i].length);

	elm = ber_read_elements(&ber, elm);
	if (elm == NULL && test_vectors[i].fail)
		return 0;
	else if (elm != NULL && test_vectors[i].fail) {
		printf("expected failure of ber_read_elements succeeded\n");
		return 1;
	} else if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	/*
	 * short form tagged elements start at the 3rd octet (i.e. position 2).
	 */
	if ((test_vectors[i].input[0] & BER_TAG_MASK) != BER_TAG_MASK) {
		pos = ber_getpos(elm);
		if (pos != 2) {
			printf("unexpected element position within "
			    "byte stream (1)\n");
			return 1;
		}
	}

	switch (elm->be_encoding) {
	case BER_TYPE_EOC:
		if (ber_get_eoc(elm) == -1) {
			printf("failed (eoc) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, ".", &val) == -1) {
			printf("failed (eoc) ber_scanf_elements\n");
			return 1;
		}
		break;
	case BER_TYPE_BOOLEAN:
		if (ber_get_boolean(elm, &b) == -1) {
			printf("failed (boolean) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "b", &b) == -1) {
			printf("failed (boolean) ber_scanf_elements\n");
			return 1;
		}
		break;
	case BER_TYPE_INTEGER:
		if (ber_get_integer(elm, &val) == -1) {
			printf("failed (int) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "i", &val) == -1) {
			printf("failed (int) ber_scanf_elements (i)\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "d", &val) == -1) {
			printf("failed (int) ber_scanf_elements (d)\n");
			return 1;
		}
		break;
	/*
	 * the current ber.c does not support bit strings
	 * however, simply processing a bit string as an octet string seems
	 * to work fine (as suspected by claudio)
	 */
	case BER_TYPE_BITSTRING:
		if (ber_get_bitstring(elm, &bstring, &len) == -1) {
			printf("failed (bit string) encoding check\n");
			return 1;
		}
		break;
	case BER_TYPE_OCTETSTRING:
		if (ber_get_ostring(elm, &ostring) == -1) {
			printf("failed (octet string) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "s", &string) == -1) {
			printf("failed (octet string) ber_scanf_elements\n");
			return 1;
		}
		break;
	case BER_TYPE_NULL:
		if (ber_get_null(elm) == -1) {
			printf("failed (null) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "0", &val) == -1) {
			printf("failed (null) ber_scanf_elements\n");
			return 1;
		}
		break;
	case BER_TYPE_OBJECT:	/* OID */
		if (ber_get_oid(elm, &oid) == -1) {
			printf("failed (object identifier) encoding check\n");
			return 1;
		}
		if (ber_scanf_elements(elm, "o", &oid) == -1) {
			printf("failed (oid) ber_scanf_elements\n");
			return 1;
		}
		break;
	case BER_TYPE_SET:
	case BER_TYPE_SEQUENCE:
		if (elm->be_sub != NULL) {
			ptr = elm->be_sub;
			if (ber_getpos(ptr) <= pos) {
				printf("unexpected element position within "
				    "byte stream\n");
				return 1;
			}
		} else {
			printf("expected sub element was not present\n");
			return 1;
		}
		break;
	default:
		printf("failed with unknown encoding (%ud)\n",
		    elm->be_encoding);
		return 1;
	}

	/*
	 * additional testing on short form tagged encoding
	 */
	if ((test_vectors[i].input[0] & BER_TAG_MASK) != BER_TAG_MASK) {
		len = ber_calc_len(elm);
		if (len != test_vectors[i].length) {
			printf("failed to calculate length\n");
			return 1;
		}

		ber.br_wbuf = NULL;
		len = ber_write_elements(&ber, elm);
		if (len != test_vectors[i].length) {
			printf("failed length check (was %zd want "
			    "%zd)\n", len, test_vectors[i].length);
			return 1;
		}

		if (memcmp(ber.br_wbuf, test_vectors[i].input,
		    test_vectors[i].length) != 0)
			return 1;
		ber_free(&ber);

	}
	ber_free_elements(elm);

	return 0;
}

int
test_ber_printf_elements_integer(void) {
	int			 val = 1, len = 0;
	struct ber_element	*elm = NULL;
	struct ber		 ber;

	unsigned char		 exp[3] = { 0x02, 0x01, 0x01 };

	elm = ber_printf_elements(elm, "d", val);
	if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	bzero(&ber, sizeof(ber));
	ber.br_wbuf = NULL;
	len = ber_write_elements(&ber, elm);
	if (len != sizeof(exp)) {
		printf("failed length check (was %d want %zd)\n", len,
		    sizeof(exp));
		return 1;
	}

	if (memcmp(ber.br_wbuf, exp, len) != 0) {
		printf("failed (int) byte stream compare\n");
		return 1;
	}

	ber_free_elements(elm);
	ber_free(&ber);
	return 0;
}

#define LDAP_REQ_BIND		0
#define LDAP_REQ_SEARCH		0
#define VERSION			3
#define	LDAP_AUTH_SIMPLE	0

int
test_ber_printf_elements_ldap_bind(void) {
	int			 len = 0, msgid = 1;
	char			*binddn = "cn=admin";
	char			*bindcred = "password";
	struct ber_element	*root = NULL, *elm = NULL;
	struct ber		 ber;

	unsigned char		 exp[] = {
	0x30, 0x1c,
	0x02, 0x01, 0x01,
	0x60, 0x17,
	0x02, 0x01, 0x03,
	0x04, 0x08, 0x63, 0x6e, 0x3d, 0x61, 0x64, 0x6d, 0x69, 0x6e,
	0x80, 0x08, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64
	};

	if ((root = ber_add_sequence(NULL)) == NULL)
		return 1;

	elm = ber_printf_elements(root, "d{tdsst", msgid,
	    BER_CLASS_APP, LDAP_REQ_BIND,
	    VERSION,
	    binddn, bindcred,
	    BER_CLASS_CONTEXT, LDAP_AUTH_SIMPLE);

	if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	bzero(&ber, sizeof(ber));
	ber.br_wbuf = NULL;
	len = ber_write_elements(&ber, root);
	if (len != sizeof(exp)) {
		printf("failed length check (was %d want %zd)\n", len,
		    sizeof(exp));
		return 1;
	}

	if (memcmp(ber.br_wbuf, exp, len) != 0) {
		printf("failed (ldap bind) byte stream compare\n");
		hexdump(ber.br_wbuf, len);
		return 1;
	}

	ber_free_elements(elm);
	ber_free(&ber);
	return 0;
}

int
test_ber_printf_elements_ldap_search(void) {
	int			 len = 0, msgid = 1;
	int			 sizelimit = 0, timelimit = 0;
	int			 typesonly = 0;
	long long		 scope = 0, deref = 0;
	char			*basedn = "ou=people";
	char			*filter = "cn";
	struct ber_element	*root = NULL, *elm = NULL;
	struct ber		 ber;

	unsigned char		 exp[] = {
	0x30, 0x05, 0x02, 0x01, 0x01, 0x60, 0x00,
	0x04, 0x09, 0x6f, 0x75, 0x3d, 0x70, 0x65, 0x6f, 0x70, 0x6c, 0x65,
	0x0a, 0x01, 0x00,
	0x0a, 0x01, 0x00,
	0x02, 0x01, 0x00,
	0x02, 0x01, 0x00,
	0x01, 0x01, 0x00,
	0x04, 0x02, 0x63, 0x6e
	};

	if ((root = ber_add_sequence(NULL)) == NULL)
		return 1;

	elm = ber_printf_elements(root, "d{t", msgid, BER_CLASS_APP,
	    LDAP_REQ_SEARCH);
	if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	elm = ber_printf_elements(root, "sEEddbs", basedn, scope, deref,
	    sizelimit, timelimit, typesonly, filter);
	if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	bzero(&ber, sizeof(ber));
	ber.br_wbuf = NULL;
	len = ber_write_elements(&ber, root);
	if (len != sizeof(exp)) {
		printf("failed length check (was %d want %zd)\n", len,
		    sizeof(exp));
		return 1;
	}

	if (memcmp(ber.br_wbuf, exp, len) != 0) {
		printf("failed (ldap search) byte stream compare\n");
		hexdump(ber.br_wbuf, len);
		return 1;
	}

	ber_free_elements(elm);
	ber_free(&ber);
	return 0;
}

int
test_ber_printf_elements_snmp_v3_encode(void) {
	int			 len = 0;
	u_int8_t		 f = 0x01;	/* verbose */
	long long		 secmodel = 3;	/* USM */
	long long		 msgid = 1, max_msg_size = 8192;
	struct ber_element	*elm = NULL;
	struct ber		 ber;

	unsigned char		 exp[] = {
	0x30, 0x0d,
	0x02, 0x01, 0x01,
	0x02, 0x02, 0x20, 0x00,
	0x04, 0x01, 0x01,
	0x02, 0x01, 0x03
	};

	elm = ber_printf_elements(elm, "{iixi}", msgid, max_msg_size,
	    &f, sizeof(f), secmodel);
	if (elm == NULL) {
		printf("failed ber_read_elements\n");
		return 1;
	}

	bzero(&ber, sizeof(ber));
	ber.br_wbuf = NULL;
	len = ber_write_elements(&ber, elm);
	if (len != sizeof(exp)) {
		printf("failed length check (was %d want %zd)\n", len,
		    sizeof(exp));
		return 1;
	}

	if (memcmp(ber.br_wbuf, exp, len) != 0) {
		printf("failed (snmp_v3_encode) byte stream compare\n");
		hexdump(ber.br_wbuf, len);
		return 1;
	}

	ber_free_elements(elm);
	ber_free(&ber);
	return 0;
}

int
main(void)
{
	extern char *__progname;

	ssize_t		len = 0;
	int		i, ret = 0;

	/*
	 * drive test vectors for ber byte stream input validation, etc.
	 */
	for (i = 0; i < sizeof(test_vectors) / sizeof(test_vectors[0]); i++) {
		if (test(i) != 0) {
			printf("FAILED: %s\n", test_vectors[i].title);
			ret = 1;
		} else
			printf("SUCCESS: %s\n", test_vectors[i].title);
	}

	/*
	 * run standalone functions for ber byte stream creation, etc.
	 * (e.g. ldap, snmpd)
	 */
	if (test_ber_printf_elements_integer() != 0)
		ret = 1;
	else
		printf("SUCCESS: test_ber_printf_elements_integer\n");

	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	if (test_ber_printf_elements_ldap_bind() != 0)
		ret = 1;
	else
		printf("SUCCESS: test_ber_printf_elements_ldap_bind\n");

	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	if (test_ber_printf_elements_ldap_search() != 0)
		ret = 1;
	else
		printf("SUCCESS: test_ber_printf_elements_ldap_search\n");
	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	if (test_ber_printf_elements_snmp_v3_encode() != 0)
		ret = 1;
	else
		printf("SUCCESS: test_ber_printf_elements_snmpd_v3_encode\n");
	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	return 0;
}
