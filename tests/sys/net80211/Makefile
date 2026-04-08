PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/net80211
BINDIR=		${TESTSDIR}

ATF_TESTS_SH=	wtap

PROGS=		sta_assoc
LIBADD.sta_assoc=	80211

MAN=

# Ensure the destination directory exists before any file install fires.
# bsd.prog.mk's SCRIPTS install path does not wire in an installdirs
# prerequisite when PROGS is also defined in the same Makefile.
beforeinstall:
	${INSTALL} -d ${DESTDIR}${TESTSDIR}

.include <bsd.test.mk>
