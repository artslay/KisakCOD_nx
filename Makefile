# Nintendo Switch build for KisakCOD.
# Requires devkitPro/libnx and Mesa Switch OpenGL/EGL libraries.
#
# The source set is generated from the engine tree instead of duplicating the
# old CMake source lists. Windows/D3D9 sources are deliberately excluded.

TARGET      := kisakcod
BUILD       := build

# Nintendo Switch application metadata.
APP_TITLE   := Call of Duty 4
APP_AUTHOR  := artslay
APP_VERSION := 1.0.0
ICON        := icon.jpg

ARCH        := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
MESA_SDK    := $(CURDIR)/mesa-sdk/opt/devkitpro/portlibs/switch
OPENAL_SDK   := $(DEVKITPRO)/portlibs/switch
CPPFLAGS    := -D__SWITCH__ -DKISAK_SWITCH -DKISAK_SP -DKISAK_OPENAL -DCINEMA -DUSE_SEPARATE_BLIT_TEXTURE \
               -I$(CURDIR)/src -I$(CURDIR)/src/gfx -I$(CURDIR)/deps \
               -I$(DEVKITPRO)/libnx/include -I$(MESA_SDK)/include -I$(OPENAL_SDK)/include
GIT_COMMIT  := $(shell git rev-parse --short=12 HEAD 2>/dev/null || printf "unknown")

CXXFLAGS    := $(ARCH) -O2 -g -ffunction-sections -fdata-sections -fno-rtti -fno-plt -fno-semantic-interposition -std=gnu++20 -MMD -MP \
               -Wno-int-to-pointer-cast -Wno-volatile -Wno-conversion-null -Wno-multichar -Wno-stringop-overflow
CPPFLAGS    += -DGIT_COMMIT=\"$(GIT_COMMIT)\"
CFLAGS      := $(ARCH) -O2 -g -ffunction-sections -fdata-sections -fno-plt -std=gnu17 -MMD -MP -Wno-old-style-definition
LDFLAGS     := $(ARCH) -L$(MESA_SDK)/lib -L$(OPENAL_SDK)/lib -L$(DEVKITPRO)/libnx/lib -specs=$(DEVKITPRO)/libnx/switch.specs -Wl,--gc-sections,-Bsymbolic
LIBS        := -lGL -lEGL -lglapi -lvulkan -lexpat -lopenal -lSDL2 -lavformat -lavcodec -lswscale -lavutil -lnx -lm

include $(DEVKITPRO)/libnx/switch_rules

# Singleplayer Switch build. Keep shared engine/game code, but exclude MP, Windows and D3D9.
CPP_SOURCES := $(shell find src -type f -name '*.cpp' \
    ! -path 'src/gfx_d3d/*' \
    ! -path 'src/win32/*' \
    ! -path 'src/linux/*' \
    ! -path 'src/platform/*' \
    ! -name 'com_files.cpp' \
    ! -path 'src/groupvoice/*' \
    ! -path 'src/radiant/*' \
    ! -path 'src/*_mp/*' \
    ! -name '*_mp.cpp' \
    ! -name 'win_common.cpp' \
    ! -name 'win_shared.cpp' \
    ! -name 'snd_mss.cpp' \
    ! -name 'snd_driver.cpp' \
    ! -name 'threads.cpp' \
    ! -name 'timing.cpp' \
    ! -name 'profile.cpp' \
    ! -path 'src/physics/ode/array.cpp' \
    ! -path 'src/physics/ode/collision_trimesh*.cpp' \
    ! -name 'stack.cpp' \
    ! -name 'obstack.cpp' \
    ! -name 'testing.cpp' \
    ! -name 'scr_yacc.cpp' \
    ! -name 'scr_compiler2.cpp' )

C_SOURCES := $(shell find src -type f -name '*.c' \
    ! -path 'src/gfx_d3d/*' \
    ! -path 'src/win32/*' \
    ! -path 'src/linux/*' \
    ! -path 'src/platform/*' \
    ! -path 'src/groupvoice/*' \
    ! -path 'src/radiant/*' \
    ! -path 'src/*_mp/*' \
    ! -name '*_mp.c' \
    ! -name 'maketree.c')

# zlib is required by the engine's archive/zip loader.
C_SOURCES += $(shell find deps/zlib -type f -name '*.c' ! -name 'maketree.c')

CPP_SOURCES += src/platform/switch/switch_main.cpp src/platform/switch/switch_fs.cpp src/platform/switch/switch_threads.cpp src/platform/switch/switch_timing.cpp src/platform/switch/switch_profile.cpp src/platform/switch/switch_sys.cpp src/platform/switch/switch_live_storage.cpp
CPP_SOURCES += src/gfx_d3d/r_init_switch.cpp src/gfx_d3d/r_buffers.cpp src/gfx_d3d/r_state.cpp
CPP_SOURCES += src/gfx_d3d/r_shade.cpp src/gfx_d3d/rb_shade.cpp src/gfx_d3d/r_material.cpp
CPP_SOURCES += src/gfx_d3d/r_material_override.cpp src/gfx_d3d/r_material_switch.cpp src/gfx_d3d/rb_uploadshaders.cpp src/gfx_d3d/r_dvars.cpp
CPP_SOURCES += src/gfx_d3d/r_image.cpp src/gfx_d3d/r_image_load_common.cpp src/gfx_d3d/r_image_load_obj.cpp src/gfx_d3d/r_image_utils.cpp src/gfx_d3d/r_image_wavelet.cpp src/gfx_d3d/r_imagedecode.cpp src/gfx_d3d/r_rendertarget.cpp
CPP_SOURCES += src/gfx_d3d/r_rendercmds.cpp src/gfx_d3d/r_font.cpp src/gfx_d3d/r_cinematic.cpp src/gfx_d3d/r_cinematic_switch.cpp src/gfx_d3d/bink_switch.cpp src/gfx_d3d/r_devgui.cpp src/gfx_d3d/r_draw_pixelshader.cpp
CPP_SOURCES += src/gfx_d3d/r_model.cpp src/gfx_d3d/r_scene.cpp src/gfx_d3d/r_dpvs.cpp
CPP_SOURCES += src/gfx_d3d/r_draw_bsp.cpp src/gfx_d3d/r_draw_lit.cpp src/gfx_d3d/r_draw_staticmodel.cpp src/gfx_d3d/r_draw_xmodel.cpp src/gfx_d3d/r_model_skin.cpp
CPP_SOURCES += src/gfx_d3d/r_bsp.cpp src/gfx_d3d/r_bsp_load_obj.cpp src/gfx_d3d/r_staticmodelcache.cpp src/gfx_d3d/r_dobj_skin.cpp src/gfx_d3d/r_model_lighting.cpp src/gfx_d3d/r_reflection_probe.cpp
CPP_SOURCES += src/gfx_d3d/r_model_pose.cpp src/gfx_d3d/r_state_utils.cpp src/gfx_d3d/r_drawsurf.cpp src/gfx_d3d/r_add_bsp.cpp src/gfx_d3d/r_add_staticmodel.cpp
CPP_SOURCES += src/gfx_d3d/r_dpvs_dynmodel.cpp src/gfx_d3d/r_dpvs_entity.cpp src/gfx_d3d/r_dpvs_sceneent.cpp src/gfx_d3d/r_dpvs_static.cpp
CPP_SOURCES += src/gfx_d3d/r_marks.cpp
CPP_SOURCES += src/gfx_d3d/r_utils.cpp src/gfx_d3d/r_warn.cpp src/gfx_d3d/rb_state.cpp src/gfx_d3d/rb_logfile.cpp
CPP_SOURCES += src/gfx_d3d/r_debug.cpp src/gfx_d3d/r_debug_alloc.cpp src/gfx_d3d/rb_debug.cpp src/gfx_d3d/rb_light.cpp src/gfx_d3d/rb_postfx.cpp src/gfx_d3d/rb_imagefilter.cpp src/gfx_d3d/rb_depthprepass.cpp src/gfx_d3d/rb_showcollision.cpp
CPP_SOURCES += src/gfx_d3d/r_sky.cpp src/gfx_d3d/r_shadowcookie.cpp src/gfx_d3d/rb_shadowcookie.cpp src/gfx_d3d/rb_sunshadow.cpp src/gfx_d3d/rb_spotshadow.cpp
CPP_SOURCES += src/gfx_d3d/r_workercmds.cpp src/gfx_d3d/r_workercmds_common.cpp src/gfx_d3d/r_spotshadow.cpp src/gfx_d3d/r_sunshadow.cpp
CPP_SOURCES += src/gfx_d3d/rb_fog.cpp src/gfx_d3d/r_fog.cpp src/gfx_d3d/r_draw_sunshadow.cpp
CPP_SOURCES += src/gfx_d3d/rb_backend.cpp
CPP_SOURCES += src/gfx_d3d/r_cmdbuf.cpp src/gfx_d3d/rb_stats.cpp src/gfx_d3d/rb_drawprofile.cpp src/gfx_d3d/rb_draw3d.cpp
CPP_SOURCES += src/gfx_d3d/rb_pixelcost.cpp src/gfx_d3d/r_pixelcost_load_obj.cpp src/gfx_d3d/rb_sky.cpp
CPP_SOURCES += src/gfx_d3d/r_water.cpp src/gfx_d3d/r_water_load_obj.cpp
CPP_SOURCES += src/gfx_d3d/r_meshdata.cpp src/gfx_d3d/r_draw_method.cpp src/gfx_d3d/r_pretess.cpp
CPP_SOURCES += src/gfx_d3d/r_draw_material.cpp src/gfx_d3d/r_draw_shadowable_light.cpp src/gfx_d3d/rb_tess.cpp
CPP_SOURCES += src/gfx_d3d/r_add_cmdbuf.cpp src/gfx_d3d/r_staticmodel.cpp src/gfx_d3d/r_xsurface.cpp src/gfx_d3d/r_reflection_probe_load_obj.cpp
CPP_SOURCES += src/gfx_d3d/r_light.cpp src/gfx_d3d/r_light_load_obj.cpp src/gfx_d3d/r_primarylights.cpp src/gfx_d3d/r_outdoor.cpp
CPP_SOURCES += src/physics/ode/collision_trimesh_box.cpp

CPP_OBJECTS := $(CPP_SOURCES:%.cpp=$(BUILD)/%.o)
C_OBJECTS   := $(C_SOURCES:%.c=$(BUILD)/%.o)
OBJECTS     := $(CPP_OBJECTS) $(C_OBJECTS)

.DEFAULT_GOAL := all

.PHONY: all clean print-sources progress-init progress-done ALWAYS

TOTAL_OBJECTS := $(words $(OBJECTS))
PROGRESS_FILE := $(BUILD)/.compile_count
PROGRESS_LOCK := $(BUILD)/.compile_count.lock

progress-init:
	@mkdir -p $(BUILD)
	@printf '0' > $(PROGRESS_FILE)
	@rm -rf $(PROGRESS_LOCK)
	@printf 'Switch build: 0/%s files compiled\n' "$(TOTAL_OBJECTS)"

progress-done: $(TARGET).nro
	@done=$$(cat "$(PROGRESS_FILE)" 2>/dev/null || printf '0'); \
	printf 'Compiled: %s/%s files\n' "$$done" "$(TOTAL_OBJECTS)"

all: progress-init $(TARGET).nro progress-done

$(TARGET).nacp: $(MAKEFILE_LIST)
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp progress-init
	@elf2nro $< $@ --nacp=$(TARGET).nacp --icon=$(CURDIR)/$(ICON)

$(TARGET).elf: $(OBJECTS)
	@$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

# Always rebuild the tiny build-number object so __DATE__/__TIME__ changes on every plain `make`.
$(BUILD)/src/buildnumber.o: src/buildnumber.cpp ALWAYS

ALWAYS:

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@while ! mkdir "$(PROGRESS_LOCK)" 2>/dev/null; do sleep 0.01; done; \
	count=$$(cat "$(PROGRESS_FILE)" 2>/dev/null || printf '0'); \
	count=$$((count + 1)); \
	printf '%s' "$$count" > "$(PROGRESS_FILE)"; \
	rmdir "$(PROGRESS_LOCK)"; \
	printf '  CXX [%s/%s] %s\n' "$$count" "$(TOTAL_OBJECTS)" "$(notdir $<)"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
	@while ! mkdir "$(PROGRESS_LOCK)" 2>/dev/null; do sleep 0.01; done; \
	count=$$(cat "$(PROGRESS_FILE)" 2>/dev/null || printf '0'); \
	count=$$((count + 1)); \
	printf '%s' "$$count" > "$(PROGRESS_FILE)"; \
	rmdir "$(PROGRESS_LOCK)"; \
	printf '  CC  [%s/%s] %s\n' "$$count" "$(TOTAL_OBJECTS)" "$(notdir $<)"

-include $(OBJECTS:.o=.d)

print-sources:
	@printf '%s\n' $(CPP_SOURCES) $(C_SOURCES)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp
