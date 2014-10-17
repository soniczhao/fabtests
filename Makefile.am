AM_CFLAGS = -g -Wall -D_GNU_SOURCE -I$(srcdir)/include

bin_PROGRAMS = \
	simple/fi_info \
	simple/fi_pingpong \
	simple/fi_bw \
	simple/fi_exp \
	simple/fi_exp_show \
	simple/ibv_exp \
	simple/ibv_exp_show \
	simple/fi_write_lat \
	simple/fi_write_bw \
	simple/fi_read_lat \
	simple/fi_read_bw \
	simple/fi_ud_pingpong \
	ported/libibverbs/fi_rc_pingpong

simple_fi_info_SOURCES = \
	simple/info.c \
	common/shared.c

simple_fi_pingpong_SOURCES = \
	simple/pingpong.c \
	common/shared.c

simple_fi_bw_SOURCES = \
	simple/bw.c \
	common/shared.c

simple_fi_exp_SOURCES = \
	simple/exp.c

simple_ibv_exp_SOURCES = \
	simple/ibv_exp.c
simple_ibv_exp_LDFLAGS = \
	-lrdmacm -libverbs

simple_fi_exp_show_SOURCES = \
	simple/exp_show.c

simple_ibv_exp_show_SOURCES = \
	simple/ibv_exp_show.c
simple_ibv_exp_show_LDFLAGS = \
	-lrdmacm -libverbs

simple_fi_write_lat_SOURCES = \
	simple/write_lat.c \
	common/shared.c

simple_fi_write_bw_SOURCES = \
	simple/write_bw.c \
	common/shared.c

simple_fi_read_lat_SOURCES = \
	simple/read_lat.c \
	common/shared.c

simple_fi_read_bw_SOURCES = \
	simple/read_bw.c \
	common/shared.c

simple_fi_ud_pingpong_SOURCES = \
	simple/ud_pingpong.c \
	common/shared.c

ported_libibverbs_fi_rc_pingpong_SOURCES = \
	ported/libibverbs/rc_pingpong.c

man_MANS = man/fabtests.7

EXTRA_DIST = common/shared.h $(man_MANS)
