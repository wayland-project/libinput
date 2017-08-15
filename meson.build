project('libinput', 'c', 'cpp',
	version : '1.8.0',
	license : 'MIT/Expat',
	default_options : [ 'c_std=gnu99', 'warning_level=2' ],
	meson_version : '>= 0.40.0')

libinput_version = meson.project_version().split('.')

# We use libtool-version numbers because it's easier to understand.
# Before making a release, the libinput_so_*
# numbers should be modified. The components are of the form C:R:A.
# a) If binary compatibility has been broken (eg removed or changed interfaces)
#    change to C+1:0:0.
# b) If interfaces have been changed or added, but binary compatibility has
#    been preserved, change to C+1:0:A+1
# c) If the interface is the same as the previous version, change to C:R+1:A
libinput_lt_c=23
libinput_lt_r=0
libinput_lt_a=13

# convert to soname
libinput_so_version = '@0@.@1@.@2@'.format((libinput_lt_c - libinput_lt_a),
                                            libinput_lt_a, libinput_lt_r)

# Compiler setup
cc = meson.get_compiler('c')
cppflags = ['-Wno-unused-parameter', '-g', '-fvisibility=hidden']
cflags = cppflags + ['-Wmissing-prototypes', '-Wstrict-prototypes']
add_project_arguments(cflags, language : 'c')
add_project_arguments(cppflags, language : 'cpp')

config_h = configuration_data()
config_h.set('_GNU_SOURCE', '1')

prefix = '''#define _GNU_SOURCE 1
#include <assert.h>
'''
if cc.get_define('static_assert', prefix : prefix) == ''
	config_h.set('static_assert(...)', '/* */')
endif

# Dependencies
pkgconfig = import('pkgconfig')
dep_udev = dependency('libudev')
dep_mtdev = dependency('mtdev', version : '>= 1.1.0')
dep_libevdev = dependency('libevdev', version : '>= 0.4')
dep_lm = cc.find_library('m', required : false)
dep_rt = cc.find_library('rt', required : false)

# Include directories
includes_include = include_directories('include')
includes_src = include_directories('src')

############ libwacom configuration ############

have_libwacom = get_option('libwacom')
config_h.set10('HAVE_LIBWACOM', have_libwacom)
if have_libwacom
	dep_libwacom = dependency('libwacom', version : '>= 0.20')

	code = '''
	#include <libwacom/libwacom.h>
	int main(void) { libwacom_get_paired_device(NULL); }
	'''
	result = cc.links(code,
			  name : 'libwacom_get_paired_device check',
			  dependencies : dep_libwacom)
	config_h.set10('HAVE_LIBWACOM_GET_PAIRED_DEVICE', result)
else
	dep_libwacom = declare_dependency()
endif

############ udev bits ############

udev_dir = get_option('udev-dir')
if udev_dir == ''
	udev_dir = join_paths(get_option('prefix'), 'lib', 'udev')
endif
udev_rules_dir = join_paths(udev_dir, 'rules.d')
udev_hwdb_dir = join_paths(udev_dir, 'hwdb.d')

executable('libinput-device-group',
	   'udev/libinput-device-group.c',
	   dependencies : [dep_udev, dep_libwacom],
	   include_directories : [includes_src, includes_include],
	   install : true,
	   install_dir : udev_dir)
executable('libinput-model-quirks',
	   'udev/libinput-model-quirks.c',
	   dependencies : dep_udev,
	   include_directories : [includes_src, includes_include],
	   install : true,
	   install_dir : udev_dir)

model_quirks = files('udev/90-libinput-model-quirks.hwdb')

parse_hwdb_py = find_program('udev/parse_hwdb.py')
test('parse-hwdb',
     parse_hwdb_py,
     args : model_quirks)

udev_rules_config = configuration_data()
udev_rules_config.set('UDEV_TEST_PATH', '')
configure_file(input : 'udev/80-libinput-device-groups.rules.in',
	       output : '80-libinput-device-groups.rules',
	       install : true,
	       install_dir : udev_rules_dir,
	       configuration : udev_rules_config)
configure_file(input : 'udev/90-libinput-model-quirks.rules.in',
	       output : '90-libinput-model-quirks.rules',
	       install : true,
	       install_dir : udev_rules_dir,
	       configuration : udev_rules_config)
configure_file(input : model_quirks,
	       output : '90-libinput-model-quirks.hwdb',
	       install : true,
	       install_dir : udev_hwdb_dir,
	       configuration : udev_rules_config)

litest_udev_rules_config = configuration_data()
litest_udev_rules_config.set('UDEV_TEST_PATH', meson.build_root() + '/')
litest_groups_rules_file = configure_file(input : 'udev/80-libinput-device-groups.rules.in',
	       output : '80-libinput-device-groups-litest.rules',
	       install : false,
	       configuration : litest_udev_rules_config)
litest_model_quirks_file = configure_file(input : 'udev/90-libinput-model-quirks.rules.in',
	       output : '90-libinput-model-quirks-litest.rules',
	       install : false,
	       configuration : litest_udev_rules_config)

############ libinput-util.a ############
src_libinput_util = [
		'src/libinput-util.c',
		'src/libinput-util.h'
]
libinput_util = static_library('libinput-util',
			       src_libinput_util,
			       dependencies : dep_udev,
			       include_directories : includes_include)
dep_libinput_util = declare_dependency(link_with : libinput_util)

############ libfilter.a ############
src_libfilter = [
		'src/filter.c',
		'src/filter.h',
		'src/filter-private.h'
]
libfilter = static_library('filter', src_libfilter)
dep_libfilter = declare_dependency(link_with : libfilter)

############ libinput.so ############
install_headers('src/libinput.h')
src_libinput = [
	'src/libinput.c',
	'src/libinput.h',
	'src/libinput-private.h',
	'src/evdev.c',
	'src/evdev.h',
	'src/evdev-lid.c',
	'src/evdev-middle-button.c',
	'src/evdev-mt-touchpad.c',
	'src/evdev-mt-touchpad.h',
	'src/evdev-mt-touchpad-tap.c',
	'src/evdev-mt-touchpad-buttons.c',
	'src/evdev-mt-touchpad-edge-scroll.c',
	'src/evdev-mt-touchpad-gestures.c',
	'src/evdev-tablet.c',
	'src/evdev-tablet.h',
	'src/evdev-tablet-pad.c',
	'src/evdev-tablet-pad.h',
	'src/evdev-tablet-pad-leds.c',
	'src/filter.c',
	'src/filter.h',
	'src/filter-private.h',
	'src/path-seat.h',
	'src/path-seat.c',
	'src/udev-seat.c',
	'src/udev-seat.h',
	'src/timer.c',
	'src/timer.h',
	'include/linux/input.h'
]
deps_libinput = [
	dep_mtdev,
	dep_udev,
	dep_libevdev,
	dep_lm,
	dep_rt,
	dep_libwacom,
	dep_libinput_util
]

libinput_version_h_config = configuration_data()
libinput_version_h_config.set('LIBINPUT_VERSION_MAJOR', libinput_version[0])
libinput_version_h_config.set('LIBINPUT_VERSION_MINOR', libinput_version[1])
libinput_version_h_config.set('LIBINPUT_VERSION_MICRO', libinput_version[2])
libinput_version_h_config.set('LIBINPUT_VERSION', meson.project_version())

libinput_version_h = configure_file(
		input : 'src/libinput-version.h.in',
		output : 'libinput-version.h',
		configuration : libinput_version_h_config,
		install : false,
)

mapfile = join_paths(meson.source_root(), 'src', 'libinput.sym')
version_flag = '-Wl,--version-script,@0@'.format(mapfile)
lib_libinput = shared_library('input',
		src_libinput,
		include_directories : [include_directories('.'), includes_include],
		dependencies : deps_libinput,
		version : libinput_so_version,
		link_args : version_flag,
		link_depends : mapfile,
		install : true
		)

dep_libinput = declare_dependency(
		link_with : lib_libinput,
		dependencies : deps_libinput)

pkgconfig.generate(
	filebase : 'libinput',
	name : 'Libinput',
	description : 'Input device library',
	version : meson.project_version(),
	libraries : lib_libinput
)

# Restore the SELinux context for the libinput.so.a.b.c on install
# meson bug https://github.com/mesonbuild/meson/issues/1967
meson.add_install_script('src/libinput-restore-selinux-context.sh',
			 get_option('libdir'),
			 lib_libinput.full_path())

############ documentation ############

if get_option('documentation')
	doxygen = find_program('doxygen')
	doxygen_version_cmd = run_command(doxygen.path(), '--version')
	if doxygen_version_cmd.returncode() != 0
		error('Command "doxygen --version" failed.')
	endif
	doxygen_version = doxygen_version_cmd.stdout()
	if doxygen_version.version_compare('< 1.8.3')
		error('doxygen needs to be at least version 1.8.3 (have @0@)'.format(doxygen_version))
	endif
	dot = find_program('dot')
	grep = find_program('grep')
	dot_version_cmd = run_command(dot.path(), '-V')
	if dot_version_cmd.returncode() != 0
		error('Command "dot -V" failed.')
	endif
	# dot -V output is (to stderr):
	# 	dot - graphviz version 2.38.0 (20140413.2041)
	dot_version = dot_version_cmd.stderr().split(' ')[4]
	if dot_version.version_compare('< 2.26')
		error('Graphviz dot needs to be at least version 2.26 (have @0@)'.format(dot_version))
	endif

	src_doxygen = [
		# source files
		meson.source_root() + '/src/libinput.h',
		# written docs
		meson.source_root() + '/doc/absolute-axes.dox',
		meson.source_root() + '/doc/absolute-coordinate-ranges.dox',
		meson.source_root() + '/doc/building.dox',
		meson.source_root() + '/doc/button_debouncing.dox',
		meson.source_root() + '/doc/clickpad-softbuttons.dox',
		meson.source_root() + '/doc/contributing.dox',
		meson.source_root() + '/doc/device-configuration-via-udev.dox',
		meson.source_root() + '/doc/faqs.dox',
		meson.source_root() + '/doc/gestures.dox',
		meson.source_root() + '/doc/middle-button-emulation.dox',
		meson.source_root() + '/doc/normalization-of-relative-motion.dox',
		meson.source_root() + '/doc/palm-detection.dox',
		meson.source_root() + '/doc/page-hierarchy.dox',
		meson.source_root() + '/doc/pointer-acceleration.dox',
		meson.source_root() + '/doc/reporting-bugs.dox',
		meson.source_root() + '/doc/scrolling.dox',
		meson.source_root() + '/doc/seats.dox',
		meson.source_root() + '/doc/switches.dox',
		meson.source_root() + '/doc/t440-support.dox',
		meson.source_root() + '/doc/tablet-support.dox',
		meson.source_root() + '/doc/tapping.dox',
		meson.source_root() + '/doc/test-suite.dox',
		meson.source_root() + '/doc/timestamps.dox',
		meson.source_root() + '/doc/tools.dox',
		meson.source_root() + '/doc/touchpad-jumping-cursors.dox',
		meson.source_root() + '/doc/touchpad-pressure.dox',
		meson.source_root() + '/doc/touchpads.dox',
		meson.source_root() + '/doc/what-is-libinput.dox',
		# dot drawings
		meson.source_root() + '/doc/dot/seats-sketch.gv',
		meson.source_root() + '/doc/dot/seats-sketch-libinput.gv',
		meson.source_root() + '/doc/dot/libinput-stack-wayland.gv',
		meson.source_root() + '/doc/dot/libinput-stack-xorg.gv',
		meson.source_root() + '/doc/dot/libinput-stack-gnome.gv',
		meson.source_root() + '/doc/dot/evemu.gv',
		# svgs
		meson.source_root() + '/doc/svg/button-scrolling.svg',
		meson.source_root() + '/doc/svg/clickfinger.svg',
		meson.source_root() + '/doc/svg/clickfinger-distance.svg',
		meson.source_root() + '/doc/svg/edge-scrolling.svg',
		meson.source_root() + '/doc/svg/gesture-2fg-ambiguity.svg',
		meson.source_root() + '/doc/svg/palm-detection.svg',
		meson.source_root() + '/doc/svg/pinch-gestures.svg',
		meson.source_root() + '/doc/svg/pinch-gestures-softbuttons.svg',
		meson.source_root() + '/doc/svg/ptraccel-linear.svg',
		meson.source_root() + '/doc/svg/ptraccel-low-dpi.svg',
		meson.source_root() + '/doc/svg/ptraccel-touchpad.svg',
		meson.source_root() + '/doc/svg/ptraccel-trackpoint.svg',
		meson.source_root() + '/doc/svg/software-buttons.svg',
		meson.source_root() + '/doc/svg/swipe-gestures.svg',
		meson.source_root() + '/doc/svg/tablet-axes.svg',
		meson.source_root() + '/doc/svg/tablet-cintiq24hd-modes.svg',
		meson.source_root() + '/doc/svg/tablet-interfaces.svg',
		meson.source_root() + '/doc/svg/tablet-intuos-modes.svg',
		meson.source_root() + '/doc/svg/tablet-left-handed.svg',
		meson.source_root() + '/doc/svg/tablet-out-of-bounds.svg',
		meson.source_root() + '/doc/svg/tablet.svg',
		meson.source_root() + '/doc/svg/tap-n-drag.svg',
		meson.source_root() + '/doc/svg/thumb-detection.svg',
		meson.source_root() + '/doc/svg/top-software-buttons.svg',
		meson.source_root() + '/doc/svg/touchscreen-gestures.svg',
		meson.source_root() + '/doc/svg/twofinger-scrolling.svg',
		# style files
		meson.source_root() + '/doc/style/header.html',
		meson.source_root() + '/doc/style/footer.html',
		meson.source_root() + '/doc/style/customdoxygen.css',
		meson.source_root() + '/doc/style/bootstrap.css',
		meson.source_root() + '/doc/style/libinputdoxygen.css',
		meson.source_root() + '/README.md'
	]

	doc_config = configuration_data()
	doc_config.set('PACKAGE_NAME', meson.project_name())
	doc_config.set('PACKAGE_VERSION', meson.project_version())
	doc_config.set('top_srcdir', meson.source_root())
	doc_config.set('INPUT', ' '.join(src_doxygen))

	doxyfile = configure_file(input : 'doc/libinput.doxygen.in',
				  output : 'libinput.doxygen',
				  configuration : doc_config,
				  install : false)
	custom_target('doxygen',
		      input : [ doxyfile ] + src_doxygen,
		      output : [ 'html' ],
		      command : [ doxygen, doxyfile ],
		      install : false,
		      build_by_default : true)

endif

############ tools ############
libinput_tool_path = join_paths(get_option('prefix'), get_option('libexecdir'), 'libinput')
config_h.set_quoted('LIBINPUT_TOOL_PATH', libinput_tool_path)
tools_shared_sources = [ 'tools/shared.c',
			 'tools/shared.h' ]
deps_tools_shared = [ dep_libinput, dep_libevdev ]
lib_tools_shared = static_library('tools_shared',
				  tools_shared_sources,
				  include_directories : [includes_src, includes_include],
				  dependencies : deps_tools_shared)
dep_tools_shared = declare_dependency(link_with : lib_tools_shared,
				      dependencies : deps_tools_shared)

man_config = configuration_data()
man_config.set('LIBINPUT_VERSION', meson.project_version())

deps_tools = [ dep_tools_shared, dep_libinput ]
libinput_debug_events_sources = [ 'tools/libinput-debug-events.c' ]
executable('libinput-debug-events',
	   libinput_debug_events_sources,
	   dependencies : deps_tools,
	   include_directories : [includes_src, includes_include],
	   install_dir : libinput_tool_path,
	   install : true
	   )
configure_file(input : 'tools/libinput-debug-events.man',
	       output : 'libinput-debug-events.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

libinput_list_devices_sources = [ 'tools/libinput-list-devices.c' ]
executable('libinput-list-devices',
	   libinput_list_devices_sources,
	   dependencies : deps_tools,
	   include_directories : [includes_src, includes_include],
	   install_dir : libinput_tool_path,
	   install : true,
	   )
configure_file(input : 'tools/libinput-list-devices.man',
	       output : 'libinput-list-devices.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

libinput_measure_sources = [ 'tools/libinput-measure.c' ]
executable('libinput-measure',
	   libinput_measure_sources,
	   dependencies : deps_tools,
	   include_directories : [includes_src, includes_include],
	   install_dir : libinput_tool_path,
	   install : true,
	   )
configure_file(input : 'tools/libinput-measure.man',
	       output : 'libinput-measure.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

libinput_measure_touchpad_tap_sources = [ 'tools/libinput-measure-touchpad-tap.c' ]
executable('libinput-measure-touchpad-tap',
	   libinput_measure_touchpad_tap_sources,
	   dependencies : deps_tools,
	   include_directories : [includes_src, includes_include],
	   install_dir : libinput_tool_path,
	   install : true,
	   )
configure_file(input : 'tools/libinput-measure-touchpad-tap.man',
	       output : 'libinput-measure-touchpad-tap.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

install_data('tools/libinput-measure-touchpad-pressure',
	     install_dir : libinput_tool_path)
configure_file(input : 'tools/libinput-measure-touchpad-pressure.man',
	       output : 'libinput-measure-touchpad-pressure.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )
install_data('tools/libinput-measure-touch-size',
	     install_dir : libinput_tool_path)
configure_file(input : 'tools/libinput-measure-touch-size.man',
	       output : 'libinput-measure-touch-size.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

install_data('tools/libinput-measure-trackpoint-range',
            install_dir : libinput_tool_path)
configure_file(input : 'tools/libinput-measure-trackpoint-range.man',
	       output : 'libinput-measure-trackpoint-range.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

if get_option('debug-gui')
	dep_gtk = dependency('gtk+-3.0')
	dep_cairo = dependency('cairo')
	dep_glib = dependency('glib-2.0')

	debug_gui_sources = [ 'tools/libinput-debug-gui.c' ]
	deps_debug_gui = [
			dep_gtk,
			dep_cairo,
			dep_glib,
			] + deps_tools
	executable('libinput-debug-gui',
		   debug_gui_sources,
		   dependencies : deps_debug_gui,
		   include_directories : [includes_src, includes_include],
		   install_dir : libinput_tool_path,
		   install : true
		   )
	configure_file(input : 'tools/libinput-debug-gui.man',
		       output : 'libinput-debug-gui.1',
		       configuration : man_config,
		       install : true,
		       install_dir : join_paths(get_option('mandir'), 'man1')
		       )
endif

libinput_sources = [ 'tools/libinput-tool.c' ]

executable('libinput',
	   libinput_sources,
	   dependencies : deps_tools,
	   include_directories : [includes_src, includes_include],
	   install : true
	   )
configure_file(input : 'tools/libinput.man',
	       output : 'libinput.1',
	       configuration : man_config,
	       install : true,
	       install_dir : join_paths(get_option('mandir'), 'man1')
	       )

meson.add_install_script('tools/install-compat-scripts.sh')

ptraccel_debug_sources = [ 'tools/ptraccel-debug.c' ]
executable('ptraccel-debug',
	   ptraccel_debug_sources,
	   dependencies : [ dep_libfilter, dep_libinput ],
	   include_directories : [includes_src, includes_include],
	   install : false
	   )

############ tests ############

if get_option('tests')
	dep_check = dependency('check', version : '>= 0.9.10')
	valgrind = find_program('valgrind')
	addr2line = find_program('addr2line')

	if addr2line.found()
		config_h.set('HAVE_ADDR2LINE', '1')
		config_h.set_quoted('ADDR2LINE', addr2line.path())
	endif

	dep_libunwind = dependency('libunwind', required : false)
	config_h.set10('HAVE_LIBUNWIND', dep_libunwind.found())

	lib_litest_sources = [
		'test/litest.h',
		'test/litest-int.h',
		'test/litest-device-acer-hawaii-keyboard.c',
		'test/litest-device-acer-hawaii-touchpad.c',
		'test/litest-device-alps-semi-mt.c',
		'test/litest-device-alps-dualpoint.c',
		'test/litest-device-anker-mouse-kbd.c',
		'test/litest-device-apple-appletouch.c',
		'test/litest-device-apple-internal-keyboard.c',
		'test/litest-device-apple-magicmouse.c',
		'test/litest-device-asus-rog-gladius.c',
		'test/litest-device-atmel-hover.c',
		'test/litest-device-bcm5974.c',
		'test/litest-device-calibrated-touchscreen.c',
		'test/litest-device-cyborg-rat-5.c',
		'test/litest-device-elantech-touchpad.c',
		'test/litest-device-generic-singletouch.c',
		'test/litest-device-gpio-keys.c',
		'test/litest-device-huion-pentablet.c',
		'test/litest-device-keyboard.c',
		'test/litest-device-keyboard-all-codes.c',
		'test/litest-device-keyboard-razer-blackwidow.c',
		'test/litest-device-lid-switch.c',
		'test/litest-device-lid-switch-surface3.c',
		'test/litest-device-logitech-trackball.c',
		'test/litest-device-nexus4-touch-screen.c',
		'test/litest-device-magic-trackpad.c',
		'test/litest-device-mouse.c',
		'test/litest-device-mouse-wheel-tilt.c',
		'test/litest-device-mouse-roccat.c',
		'test/litest-device-mouse-low-dpi.c',
		'test/litest-device-mouse-wheel-click-angle.c',
		'test/litest-device-mouse-wheel-click-count.c',
		'test/litest-device-ms-surface-cover.c',
		'test/litest-device-protocol-a-touch-screen.c',
		'test/litest-device-qemu-usb-tablet.c',
		'test/litest-device-synaptics.c',
		'test/litest-device-synaptics-hover.c',
		'test/litest-device-synaptics-i2c.c',
		'test/litest-device-synaptics-rmi4.c',
		'test/litest-device-synaptics-st.c',
		'test/litest-device-synaptics-t440.c',
		'test/litest-device-synaptics-x1-carbon-3rd.c',
		'test/litest-device-trackpoint.c',
		'test/litest-device-touch-screen.c',
		'test/litest-device-touchscreen-fuzz.c',
		'test/litest-device-wacom-bamboo-16fg-pen.c',
		'test/litest-device-wacom-cintiq-12wx-pen.c',
		'test/litest-device-wacom-cintiq-13hdt-finger.c',
		'test/litest-device-wacom-cintiq-13hdt-pad.c',
		'test/litest-device-wacom-cintiq-13hdt-pen.c',
		'test/litest-device-wacom-cintiq-24hd-pen.c',
		'test/litest-device-wacom-cintiq-24hdt-pad.c',
		'test/litest-device-wacom-ekr.c',
		'test/litest-device-wacom-hid4800-pen.c',
		'test/litest-device-wacom-intuos3-pad.c',
		'test/litest-device-wacom-intuos5-finger.c',
		'test/litest-device-wacom-intuos5-pad.c',
		'test/litest-device-wacom-intuos5-pen.c',
		'test/litest-device-wacom-isdv4-e6-pen.c',
		'test/litest-device-wacom-isdv4-e6-finger.c',
		'test/litest-device-waltop-tablet.c',
		'test/litest-device-wheel-only.c',
		'test/litest-device-xen-virtual-pointer.c',
		'test/litest-device-vmware-virtual-usb-mouse.c',
		'test/litest-device-yubikey.c',
		'test/litest.c'
	]

	dep_dl = cc.find_library('dl')
	deps_litest = [
		dep_libinput,
		dep_check,
		dep_libunwind,
		dep_udev,
		dep_libevdev,
		dep_dl,
		dep_lm
	]

	configure_file(input : 'udev/80-libinput-test-device.rules',
		       output : '80-libinput-test-device.rules',
		       install : false,
		       configuration : udev_rules_config)

	config_h.set_quoted('LIBINPUT_MODEL_QUIRKS_UDEV_RULES_FILE',
			    join_paths(meson.build_root(), '90-libinput-model-quirks.rules'))
	config_h.set_quoted('LIBINPUT_MODEL_QUIRKS_UDEV_HWDB_FILE',
			    join_paths(meson.build_root(), '90-libinput-model-quirks.hwdb'))
	config_h.set_quoted('LIBINPUT_TEST_DEVICE_RULES_FILE',
			    join_paths(meson.build_root(), '80-libinput-test-device.rules'))
	config_h.set_quoted('LIBINPUT_DEVICE_GROUPS_RULES_FILE',
			    join_paths(meson.build_root(), '80-libinput-device-groups.rules'))
	lib_litest = static_library('litest',
				    lib_litest_sources,
				    include_directories : [includes_src, includes_include],
				    dependencies : deps_litest)
	dep_litest = declare_dependency(link_with : lib_litest,
					dependencies : deps_litest)

	def_no_main = '-DLITEST_NO_MAIN'
	def_disable_backtrace = '-DLITEST_DISABLE_BACKTRACE_LOGGING'
	defs_litest_selftest = [
		def_no_main,
		def_disable_backtrace
	]
	test_litest_selftest_sources = [
		'test/litest-selftest.c',
		'test/litest.c',
		'test/litest-int.h',
		'test/litest.h'
	]
	deps_litest_selftest = [dep_litest]
	test_litest_selftest = executable('test-litest-selftest',
					  test_litest_selftest_sources,
					  include_directories : [includes_src, includes_include],
					  dependencies : deps_litest_selftest,
					  c_args : defs_litest_selftest,
					  install : false)
	test('test-litest-selftest', test_litest_selftest)

	test_symbols_leak = find_program('test/symbols-leak-test.in')
	test('symbols-leak-test',
	     test_symbols_leak,
	     args : [ meson.current_source_dir() ])

	libinput_test_runner_sources = [
		'src/libinput-util.h',
		'src/libinput-util.c',
		'test/test-udev.c',
		'test/test-path.c',
		'test/test-pointer.c',
		'test/test-touch.c',
		'test/test-log.c',
		'test/test-tablet.c',
		'test/test-pad.c',
		'test/test-touchpad.c',
		'test/test-touchpad-tap.c',
		'test/test-touchpad-buttons.c',
		'test/test-trackpoint.c',
		'test/test-trackball.c',
		'test/test-misc.c',
		'test/test-keyboard.c',
		'test/test-device.c',
		'test/test-gestures.c',
		'test/test-lid.c'
	]
	def_LT_VERSION = '-DLIBINPUT_LT_VERSION="@0@:@1@:@2@"'.format(libinput_lt_c, libinput_lt_r, libinput_lt_a)
	libinput_test_runner = executable('libinput-test-suite-runner',
					  libinput_test_runner_sources,
					  include_directories : include_directories('src'),
					  dependencies : dep_litest,
					  c_args : [ def_LT_VERSION ],
					  install : false)
	test('libinput-test-suite-runner',
	     libinput_test_runner,
	     timeout : 1200)

	# build-test only
        executable('test-build-pedantic',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   c_args : ['-std=c99', '-pedantic', '-Werror'],
		   install : false)
	# build-test only
        executable('test-build-std-gnuc90',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   c_args : ['-std=gnu90', '-Werror'],
		   install : false)
	# test for linking with the minimal linker flags
        executable('test-build-linker',
		   'test/build-pedantic.c',
		   include_directories : include_directories('src'),
		   dependencies : [ dep_libinput, dep_libinput_util ],
		   install : false)
	# test including from C++
	executable('test-build-cxx',
		   'test/build-cxx.cc',
		   include_directories : include_directories('src'),
		   install : false)

	valgrind_env = environment()
	valgrind_env.set('CK_FORK', 'no')
	valgrind_env.set('USING_VALGRIND', '1')
	valgrind_suppressions_file = join_paths(meson.source_root(), 'test', 'valgrind.suppressions')
	add_test_setup('valgrind',
			exe_wrapper : [ valgrind,
					'--leak-check=full',
					'--error-exitcode=3',
					'--suppressions=' + valgrind_suppressions_file ],
			env :  valgrind_env,
			timeout_multiplier : 100)
endif
############ output files ############
configure_file(output : 'config.h', install : false, configuration : config_h)
