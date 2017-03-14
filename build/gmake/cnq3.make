# GNU Make project makefile autogenerated by Premake

ifndef config
  config=debug_x32
endif

ifndef verbose
  SILENT = @
endif

.PHONY: clean prebuild prelink

ifeq ($(config),debug_x32)
  RESCOMP = windres
  TARGETDIR = ../../../.bin/debug_x32
  TARGET = $(TARGETDIR)/cnq3-x86
  OBJDIR = obj/x32/debug/cnq3
  DEFINES += -DDEBUG -D_DEBUG
  INCLUDES += -I../../code/freetype/include
  FORCE_INCLUDE +=
  ALL_CPPFLAGS += $(CPPFLAGS) -MMD -MP $(DEFINES) $(INCLUDES)
  ALL_CFLAGS += $(CFLAGS) $(ALL_CPPFLAGS) -m32 -g -Wno-unused-parameter -Wno-write-strings -pthread -x c++
  ALL_CXXFLAGS += $(CXXFLAGS) $(ALL_CFLAGS) -fno-exceptions -fno-rtti
  ALL_RESFLAGS += $(RESFLAGS) $(DEFINES) $(INCLUDES)
  LIBS += ../../../.bin/debug_x32/libbotlib.a ../../../.bin/debug_x32/librenderer.a ../../../.bin/debug_x32/libfreetype.a ../../../.bin/debug_x32/liblibjpeg-turbo.a -ldl -lm -lbacktrace -lX11 -lpthread
  LDDEPS += ../../../.bin/debug_x32/libbotlib.a ../../../.bin/debug_x32/librenderer.a ../../../.bin/debug_x32/libfreetype.a ../../../.bin/debug_x32/liblibjpeg-turbo.a
  ALL_LDFLAGS += $(LDFLAGS) -L/usr/lib32 -L../../../.bin/debug_x32 -m32 
  LINKCMD = $(CXX) -o "$@" $(OBJECTS) $(RESOURCES) $(ALL_LDFLAGS) $(LIBS)
  define PREBUILDCMDS
	@echo Running prebuild commands
	"../create_git_header.sh" "../../code/qcommon/git.h"
  endef
  define PRELINKCMDS
  endef
  define POSTBUILDCMDS
	@echo Running postbuild commands
	cp -u "../../../.bin/debug_x32/cnq3-x86" "$(QUAKE3DIR)"
  endef
all: $(TARGETDIR) $(OBJDIR) prebuild prelink $(TARGET)
	@:

endif

ifeq ($(config),debug_x64)
  RESCOMP = windres
  TARGETDIR = ../../../.bin/debug_x64
  TARGET = $(TARGETDIR)/cnq3-x64
  OBJDIR = obj/x64/debug/cnq3
  DEFINES += -DDEBUG -D_DEBUG
  INCLUDES += -I../../code/freetype/include
  FORCE_INCLUDE +=
  ALL_CPPFLAGS += $(CPPFLAGS) -MMD -MP $(DEFINES) $(INCLUDES)
  ALL_CFLAGS += $(CFLAGS) $(ALL_CPPFLAGS) -m64 -g -Wno-unused-parameter -Wno-write-strings -pthread -x c++
  ALL_CXXFLAGS += $(CXXFLAGS) $(ALL_CFLAGS) -fno-exceptions -fno-rtti
  ALL_RESFLAGS += $(RESFLAGS) $(DEFINES) $(INCLUDES)
  LIBS += ../../../.bin/debug_x64/libbotlib.a ../../../.bin/debug_x64/librenderer.a ../../../.bin/debug_x64/libfreetype.a ../../../.bin/debug_x64/liblibjpeg-turbo.a -ldl -lm -lbacktrace -lX11 -lpthread
  LDDEPS += ../../../.bin/debug_x64/libbotlib.a ../../../.bin/debug_x64/librenderer.a ../../../.bin/debug_x64/libfreetype.a ../../../.bin/debug_x64/liblibjpeg-turbo.a
  ALL_LDFLAGS += $(LDFLAGS) -L/usr/lib64 -L../../../.bin/debug_x64 -m64 
  LINKCMD = $(CXX) -o "$@" $(OBJECTS) $(RESOURCES) $(ALL_LDFLAGS) $(LIBS)
  define PREBUILDCMDS
	@echo Running prebuild commands
	"../create_git_header.sh" "../../code/qcommon/git.h"
  endef
  define PRELINKCMDS
  endef
  define POSTBUILDCMDS
	@echo Running postbuild commands
	cp -u "../../../.bin/debug_x64/cnq3-x64" "$(QUAKE3DIR)"
  endef
all: $(TARGETDIR) $(OBJDIR) prebuild prelink $(TARGET)
	@:

endif

ifeq ($(config),release_x32)
  RESCOMP = windres
  TARGETDIR = ../../../.bin/release_x32
  TARGET = $(TARGETDIR)/cnq3-x86
  OBJDIR = obj/x32/release/cnq3
  DEFINES += -DNDEBUG
  INCLUDES += -I../../code/freetype/include
  FORCE_INCLUDE +=
  ALL_CPPFLAGS += $(CPPFLAGS) -MMD -MP $(DEFINES) $(INCLUDES)
  ALL_CFLAGS += $(CFLAGS) $(ALL_CPPFLAGS) -m32 -fomit-frame-pointer -ffast-math -Os -g -msse2 -Wno-unused-parameter -Wno-write-strings -g1 -pthread -x c++
  ALL_CXXFLAGS += $(CXXFLAGS) $(ALL_CFLAGS) -fno-exceptions -fno-rtti
  ALL_RESFLAGS += $(RESFLAGS) $(DEFINES) $(INCLUDES)
  LIBS += ../../../.bin/release_x32/libbotlib.a ../../../.bin/release_x32/librenderer.a ../../../.bin/release_x32/libfreetype.a ../../../.bin/release_x32/liblibjpeg-turbo.a -ldl -lm -lbacktrace -lX11 -lpthread
  LDDEPS += ../../../.bin/release_x32/libbotlib.a ../../../.bin/release_x32/librenderer.a ../../../.bin/release_x32/libfreetype.a ../../../.bin/release_x32/liblibjpeg-turbo.a
  ALL_LDFLAGS += $(LDFLAGS) -L/usr/lib32 -L../../../.bin/release_x32 -m32 
  LINKCMD = $(CXX) -o "$@" $(OBJECTS) $(RESOURCES) $(ALL_LDFLAGS) $(LIBS)
  define PREBUILDCMDS
	@echo Running prebuild commands
	"../create_git_header.sh" "../../code/qcommon/git.h"
  endef
  define PRELINKCMDS
  endef
  define POSTBUILDCMDS
	@echo Running postbuild commands
	cp -u "../../../.bin/release_x32/cnq3-x86" "$(QUAKE3DIR)"
  endef
all: $(TARGETDIR) $(OBJDIR) prebuild prelink $(TARGET)
	@:

endif

ifeq ($(config),release_x64)
  RESCOMP = windres
  TARGETDIR = ../../../.bin/release_x64
  TARGET = $(TARGETDIR)/cnq3-x64
  OBJDIR = obj/x64/release/cnq3
  DEFINES += -DNDEBUG
  INCLUDES += -I../../code/freetype/include
  FORCE_INCLUDE +=
  ALL_CPPFLAGS += $(CPPFLAGS) -MMD -MP $(DEFINES) $(INCLUDES)
  ALL_CFLAGS += $(CFLAGS) $(ALL_CPPFLAGS) -m64 -fomit-frame-pointer -ffast-math -Os -g -msse2 -Wno-unused-parameter -Wno-write-strings -g1 -pthread -x c++
  ALL_CXXFLAGS += $(CXXFLAGS) $(ALL_CFLAGS) -fno-exceptions -fno-rtti
  ALL_RESFLAGS += $(RESFLAGS) $(DEFINES) $(INCLUDES)
  LIBS += ../../../.bin/release_x64/libbotlib.a ../../../.bin/release_x64/librenderer.a ../../../.bin/release_x64/libfreetype.a ../../../.bin/release_x64/liblibjpeg-turbo.a -ldl -lm -lbacktrace -lX11 -lpthread
  LDDEPS += ../../../.bin/release_x64/libbotlib.a ../../../.bin/release_x64/librenderer.a ../../../.bin/release_x64/libfreetype.a ../../../.bin/release_x64/liblibjpeg-turbo.a
  ALL_LDFLAGS += $(LDFLAGS) -L/usr/lib64 -L../../../.bin/release_x64 -m64 
  LINKCMD = $(CXX) -o "$@" $(OBJECTS) $(RESOURCES) $(ALL_LDFLAGS) $(LIBS)
  define PREBUILDCMDS
	@echo Running prebuild commands
	"../create_git_header.sh" "../../code/qcommon/git.h"
  endef
  define PRELINKCMDS
  endef
  define POSTBUILDCMDS
	@echo Running postbuild commands
	cp -u "../../../.bin/release_x64/cnq3-x64" "$(QUAKE3DIR)"
  endef
all: $(TARGETDIR) $(OBJDIR) prebuild prelink $(TARGET)
	@:

endif

OBJECTS := \
	$(OBJDIR)/cl_avi.o \
	$(OBJDIR)/cl_browser.o \
	$(OBJDIR)/cl_cgame.o \
	$(OBJDIR)/cl_cin.o \
	$(OBJDIR)/cl_console.o \
	$(OBJDIR)/cl_curl.o \
	$(OBJDIR)/cl_input.o \
	$(OBJDIR)/cl_keys.o \
	$(OBJDIR)/cl_main.o \
	$(OBJDIR)/cl_net_chan.o \
	$(OBJDIR)/cl_parse.o \
	$(OBJDIR)/cl_scrn.o \
	$(OBJDIR)/cl_ui.o \
	$(OBJDIR)/snd_codec.o \
	$(OBJDIR)/snd_codec_wav.o \
	$(OBJDIR)/snd_dma.o \
	$(OBJDIR)/snd_main.o \
	$(OBJDIR)/snd_mem.o \
	$(OBJDIR)/snd_mix.o \
	$(OBJDIR)/cm_load.o \
	$(OBJDIR)/cm_patch.o \
	$(OBJDIR)/cm_polylib.o \
	$(OBJDIR)/cm_test.o \
	$(OBJDIR)/cm_trace.o \
	$(OBJDIR)/cmd.o \
	$(OBJDIR)/common.o \
	$(OBJDIR)/crash.o \
	$(OBJDIR)/cvar.o \
	$(OBJDIR)/files.o \
	$(OBJDIR)/huffman.o \
	$(OBJDIR)/huffman_static.o \
	$(OBJDIR)/json.o \
	$(OBJDIR)/md4.o \
	$(OBJDIR)/md5.o \
	$(OBJDIR)/msg.o \
	$(OBJDIR)/net_chan.o \
	$(OBJDIR)/net_ip.o \
	$(OBJDIR)/q_math.o \
	$(OBJDIR)/q_shared.o \
	$(OBJDIR)/unzip.o \
	$(OBJDIR)/vm.o \
	$(OBJDIR)/vm_interpreted.o \
	$(OBJDIR)/vm_x86.o \
	$(OBJDIR)/sv_bot.o \
	$(OBJDIR)/sv_ccmds.o \
	$(OBJDIR)/sv_client.o \
	$(OBJDIR)/sv_game.o \
	$(OBJDIR)/sv_init.o \
	$(OBJDIR)/sv_main.o \
	$(OBJDIR)/sv_net_chan.o \
	$(OBJDIR)/sv_snapshot.o \
	$(OBJDIR)/sv_world.o \
	$(OBJDIR)/linux_glimp.o \
	$(OBJDIR)/linux_joystick.o \
	$(OBJDIR)/linux_qgl.o \
	$(OBJDIR)/linux_signals.o \
	$(OBJDIR)/linux_snd.o \
	$(OBJDIR)/unix_main.o \
	$(OBJDIR)/unix_shared.o \

RESOURCES := \

CUSTOMFILES := \

SHELLTYPE := msdos
ifeq (,$(ComSpec)$(COMSPEC))
  SHELLTYPE := posix
endif
ifeq (/bin,$(findstring /bin,$(SHELL)))
  SHELLTYPE := posix
endif

$(TARGET): $(GCH) ${CUSTOMFILES} $(OBJECTS) $(LDDEPS) $(RESOURCES)
	@echo Linking cnq3
	$(SILENT) $(LINKCMD)
	$(POSTBUILDCMDS)

$(TARGETDIR):
	@echo Creating $(TARGETDIR)
ifeq (posix,$(SHELLTYPE))
	$(SILENT) mkdir -p $(TARGETDIR)
else
	$(SILENT) mkdir $(subst /,\\,$(TARGETDIR))
endif

$(OBJDIR):
	@echo Creating $(OBJDIR)
ifeq (posix,$(SHELLTYPE))
	$(SILENT) mkdir -p $(OBJDIR)
else
	$(SILENT) mkdir $(subst /,\\,$(OBJDIR))
endif

clean:
	@echo Cleaning cnq3
ifeq (posix,$(SHELLTYPE))
	$(SILENT) rm -f  $(TARGET)
	$(SILENT) rm -rf $(OBJDIR)
else
	$(SILENT) if exist $(subst /,\\,$(TARGET)) del $(subst /,\\,$(TARGET))
	$(SILENT) if exist $(subst /,\\,$(OBJDIR)) rmdir /s /q $(subst /,\\,$(OBJDIR))
endif

prebuild:
	$(PREBUILDCMDS)

prelink:
	$(PRELINKCMDS)

ifneq (,$(PCH))
$(OBJECTS): $(GCH) $(PCH)
$(GCH): $(PCH)
	@echo $(notdir $<)
	$(SILENT) $(CXX) -x c++-header $(ALL_CXXFLAGS) -o "$@" -MF "$(@:%.gch=%.d)" -c "$<"
endif

$(OBJDIR)/cl_avi.o: ../../code/client/cl_avi.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_browser.o: ../../code/client/cl_browser.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_cgame.o: ../../code/client/cl_cgame.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_cin.o: ../../code/client/cl_cin.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_console.o: ../../code/client/cl_console.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_curl.o: ../../code/client/cl_curl.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_input.o: ../../code/client/cl_input.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_keys.o: ../../code/client/cl_keys.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_main.o: ../../code/client/cl_main.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_net_chan.o: ../../code/client/cl_net_chan.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_parse.o: ../../code/client/cl_parse.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_scrn.o: ../../code/client/cl_scrn.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cl_ui.o: ../../code/client/cl_ui.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_codec.o: ../../code/client/snd_codec.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_codec_wav.o: ../../code/client/snd_codec_wav.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_dma.o: ../../code/client/snd_dma.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_main.o: ../../code/client/snd_main.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_mem.o: ../../code/client/snd_mem.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/snd_mix.o: ../../code/client/snd_mix.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cm_load.o: ../../code/qcommon/cm_load.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cm_patch.o: ../../code/qcommon/cm_patch.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cm_polylib.o: ../../code/qcommon/cm_polylib.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cm_test.o: ../../code/qcommon/cm_test.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cm_trace.o: ../../code/qcommon/cm_trace.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cmd.o: ../../code/qcommon/cmd.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/common.o: ../../code/qcommon/common.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/crash.o: ../../code/qcommon/crash.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/cvar.o: ../../code/qcommon/cvar.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/files.o: ../../code/qcommon/files.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/huffman.o: ../../code/qcommon/huffman.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/huffman_static.o: ../../code/qcommon/huffman_static.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/json.o: ../../code/qcommon/json.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/md4.o: ../../code/qcommon/md4.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/md5.o: ../../code/qcommon/md5.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/msg.o: ../../code/qcommon/msg.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/net_chan.o: ../../code/qcommon/net_chan.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/net_ip.o: ../../code/qcommon/net_ip.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/q_math.o: ../../code/qcommon/q_math.c
	@echo $(notdir $<)
	$(SILENT) $(CC) $(ALL_CFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/q_shared.o: ../../code/qcommon/q_shared.c
	@echo $(notdir $<)
	$(SILENT) $(CC) $(ALL_CFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/unzip.o: ../../code/qcommon/unzip.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/vm.o: ../../code/qcommon/vm.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/vm_interpreted.o: ../../code/qcommon/vm_interpreted.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/vm_x86.o: ../../code/qcommon/vm_x86.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_bot.o: ../../code/server/sv_bot.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_ccmds.o: ../../code/server/sv_ccmds.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_client.o: ../../code/server/sv_client.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_game.o: ../../code/server/sv_game.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_init.o: ../../code/server/sv_init.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_main.o: ../../code/server/sv_main.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_net_chan.o: ../../code/server/sv_net_chan.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_snapshot.o: ../../code/server/sv_snapshot.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/sv_world.o: ../../code/server/sv_world.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/linux_glimp.o: ../../code/unix/linux_glimp.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/linux_joystick.o: ../../code/unix/linux_joystick.c
	@echo $(notdir $<)
	$(SILENT) $(CC) $(ALL_CFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/linux_qgl.o: ../../code/unix/linux_qgl.c
	@echo $(notdir $<)
	$(SILENT) $(CC) $(ALL_CFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/linux_signals.o: ../../code/unix/linux_signals.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/linux_snd.o: ../../code/unix/linux_snd.c
	@echo $(notdir $<)
	$(SILENT) $(CC) $(ALL_CFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/unix_main.o: ../../code/unix/unix_main.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"
$(OBJDIR)/unix_shared.o: ../../code/unix/unix_shared.cpp
	@echo $(notdir $<)
	$(SILENT) $(CXX) $(ALL_CXXFLAGS) $(FORCE_INCLUDE) -o "$@" -MF "$(@:%.o=%.d)" -c "$<"

-include $(OBJECTS:%.o=%.d)
ifneq (,$(PCH))
  -include $(OBJDIR)/$(notdir $(PCH)).d
endif