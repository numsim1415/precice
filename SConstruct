import os
import subprocess
import sys

##################################################################### FUNCTIONS

def uniqueCheckLib(conf, lib):
    """ Checks for a library and appends it to env if not already appended. """
    if conf.CheckLib(lib, autoadd=0, language="C++"):
        conf.env.AppendUnique(LIBS = [lib])
        return True
    else:
        print "ERROR: Library '" + lib + "' not found!"
        Exit(1)

def errorMissingHeader(header, usage):
    print "ERROR: Header '" + header + "' (needed for " + usage + ") not found or does not compile!"
    Exit(1)

def print_options(vars):
    """ Print all build option and if they have been modified from their default value. """
    for opt in vars.options:
        try:
            is_default = vars.args[opt.key] == opt.default
        except KeyError:
            is_default = True
        vprint(opt.key, env[opt.key], is_default, opt.help)

def vprint(name, value, default=True, description = None):
    """ Pretty prints an environment variabe with value and modified or not. """
    mod = "(default)" if default else "(modified)"
    desc = "   " + description if description else ""
    print "{0:10} {1:25} = {2!s:8}{3}".format(mod, name, value, desc)

def checkset_var(varname, default):
    """ Checks if environment variable is set, use default otherwise and print the value. """
    var = os.getenv(varname)
    if not var:
        var = default
        vprint(varname, var)
    else:
        vprint(varname, var, False)
    return var

def get_real_compiler(compiler):
    """ Gets the compiler behind the MPI compiler wrapper. """
    if compiler.startswith("mpi"):
        try:
            output = subprocess.check_output("%s -show" % compiler, shell=True)
        except (OSError, subprocess.CalledProcessError) as e:
            print "Error getting wrapped compiler from MPI compiler"
            print "Command was:", e.cmd, "Output was:", e.output
        else:
            return output.split()[0]
    else:
        return compiler



########################################################################## MAIN

vars = Variables(None, ARGUMENTS)

vars.Add(PathVariable("builddir", "Directory holding build files.", "build", PathVariable.PathAccept))
vars.Add(EnumVariable('build', 'Build type, either release or debug', "debug", allowed_values=('release', 'debug')))
vars.Add("compiler", "Compiler to use.", "g++")
vars.Add(BoolVariable("mpi", "Enables MPI-based communication and running coupling tests.", True))
vars.Add(BoolVariable("sockets", "Enables Socket-based communication.", True))
vars.Add(BoolVariable("spirit2", "Used for parsing VRML file geometries and checkpointing.", True))
vars.Add(BoolVariable("petsc", "Enable use of the Petsc linear algebra library.", True))
vars.Add(BoolVariable("python", "Used for Python scripted solver actions.", True))
vars.Add(BoolVariable("gprof", "Used in detailed performance analysis.", False))
vars.Add(EnumVariable('platform', 'Special configuration for certain platforms', "none", allowed_values=('none', 'supermuc', 'hazelhen')))

env = Environment(variables = vars, ENV = os.environ)   # For configuring build variables
conf = Configure(env) # For checking libraries, headers, ...

Help(vars.GenerateHelpText(env))
env.Append(CPPPATH = ['#src'])

print
print_options(vars)

buildpath = os.path.join(env["builddir"], "") # Ensures to have a trailing slash

print

env.Append(LIBPATH = [('#' + buildpath)])
env.Append(CCFLAGS= ['-Wall', '-std=c++11'])

# ====== Compiler Settings ======

# Produce position independent code for dynamic linking
env.Append(CCFLAGS = ['-fPIC'])

real_compiler = get_real_compiler(env["compiler"])
if real_compiler == 'icc':
    env.AppendUnique(LIBPATH = ['/usr/lib/'])
    env.Append(LIBS = ['stdc++'])
    if env["build"] == 'debug':
        env.Append(CCFLAGS = ['-align'])
    elif env["build"] == 'release':
        env.Append(CCFLAGS = ['-w', '-fast', '-align', '-ansi-alias'])
elif real_compiler == 'g++':
    pass
elif real_compiler == "clang++":
    env.Append(CCFLAGS= ['-Wsign-compare']) # sign-compare not enabled in Wall with clang.
elif real_compiler == "g++-mp-4.9":
    # Some special treatment that seems to be necessary for Mac OS.
    # See https://github.com/precice/precice/issues/2
    env.Append(LIBS = ['libstdc++.6'])
    env.AppendUnique(LIBPATH = ['/opt/local/lib/'])

env.Replace(CXX = env["compiler"])
env.Replace(CC = env["compiler"])

if not conf.CheckCXX():
    Exit(1)

# ====== Build Directories ======
if env["build"] == 'debug':
    # The Assert define does not actually switches asserts on/off, these are controlled by NDEBUG.
    # It's kept in place for some legacy code.
    env.Append(CPPDEFINES = ['Debug', 'Asserts'])
    env.Append(CCFLAGS = ['-g3', '-O0'])
    env.Append(LINKFLAGS = ["-rdynamic"]) # Gives more informative backtraces
    buildpath += "debug"
elif env["build"] == 'release':
    env.Append(CPPDEFINES = ['NDEBUG']) # Standard C++ macro which disables all asserts, also used by Eigen
    env.Append(CCFLAGS = ['-O3'])
    buildpath += "release"

# ====== PETSc ======
if env["petsc"]:
    PETSC_DIR = checkset_var("PETSC_DIR", "")
    PETSC_ARCH = checkset_var("PETSC_ARCH", "")
    
    if not env["mpi"]:
        print "Petsc requires MPI to be enabled."
        Exit(1)

    env.Append(CPPPATH = [os.path.join( PETSC_DIR, "include"),
                          os.path.join( PETSC_DIR, PETSC_ARCH, "include")])
    env.Append(LIBPATH = [os.path.join( PETSC_DIR, PETSC_ARCH, "lib")])
    if env["platform"] == "hazelhen":
        uniqueCheckLib(conf, "craypetsc_gnu_real")
    else:
        uniqueCheckLib(conf, "petsc")
else:
    env.Append(CPPDEFINES = ['PRECICE_NO_PETSC'])
    buildpath += "-nopetsc"

# ====== Eigen ======
if not conf.CheckCXXHeader("Eigen/Dense"):
    errorMissingHeader("Eigen/Dense", "Eigen")
if env["build"] == "debug":
    env.Append(CPPDEFINES = ['EIGEN_INITIALIZE_MATRICES_BY_NAN'])

# ====== Boost ======
# Needed for correct linking on Hazel Hen
# Otherwise it would link partly to old system boost, partly to newer modules boost
if env["platform"] == "hazelhen":
    env.Append(CPPPATH = [os.environ['BOOST_ROOT'] + '/include'])
    env.Append(LIBPATH = [os.environ['BOOST_ROOT'] + '/lib'])

env.Append(CPPDEFINES= ['BOOST_SPIRIT_USE_PHOENIX_V3',
                        'BOOST_ALL_DYN_LINK'])

uniqueCheckLib(conf, "boost_log")
uniqueCheckLib(conf, "boost_log_setup")
uniqueCheckLib(conf, "boost_thread")
uniqueCheckLib(conf, "boost_system")
uniqueCheckLib(conf, "boost_filesystem")
uniqueCheckLib(conf, "boost_program_options")
uniqueCheckLib(conf, "boost_unit_test_framework")

if not conf.CheckCXXHeader('boost/vmd/is_empty.hpp'):
    errorMissingHeader('boost/vmd/is_empty.hpp', 'Boost Variadic Macro Data Library')

# ====== Spirit2 ======
if not env["spirit2"]:
    env.Append(CPPDEFINES = ['PRECICE_NO_SPIRIT2'])
    buildpath += "-nospirit2"

# ====== MPI ======
if env["mpi"]:
    # Skip (deprecated) MPI C++ bindings.
    env.Append(CPPDEFINES = ['MPICH_SKIP_MPICXX'])

    if not conf.CheckHeader('mpi.h'):
        errorMissingHeader('mpi.h', 'MPI')
        
elif not env["mpi"]:
    env.Append(CPPDEFINES = ['PRECICE_NO_MPI'])
    buildpath += "-nompi"

# ====== Sockets ======
if env["sockets"]:
    pthreadLib = checkset_var('PRECICE_PTHREAD_LIB', "pthread")

    if sys.platform.startswith('win') or sys.platform.startswith('msys'):
        socketLibPath = checkset_var('PRECICE_SOCKET_LIB_PATH', "/mingw64/lib")
        socketLib = checkset_var('PRECICE_SOCKET_LIB', "ws2_32")
        socketIncPath =  checkset_var('PRECICE_SOCKET_INC_PATH', '/mingw64/include')

        env.AppendUnique(LIBPATH = [socketLibPath])
        uniqueCheckLib(conf, socketLib)
        env.AppendUnique(CPPPATH = [socketIncPath])

        if socketLib == 'ws2_32':
            if not conf.CheckHeader('winsock2.h'):
                errorMissingHeader('winsock2.h', 'Windows Sockets 2')

    uniqueCheckLib(conf, pthreadLib)
    if pthreadLib == 'pthread':
        if not conf.CheckCXXHeader('pthread.h'):
            errorMissingHeader('pthread.h', 'POSIX Threads')
else:
    env.Append(CPPDEFINES = ['PRECICE_NO_SOCKETS'])
    buildpath += "-nosockets"

# ====== Python ======
if env["python"]:
    pythonLib = checkset_var('PRECICE_PYTHON_LIB', "python2.7")
    pythonIncPath = checkset_var('PRECICE_PYTHON_INC_PATH', '/usr/include/python2.7/')
    numpyIncPath = checkset_var('PRECICE_NUMPY_INC_PATH',  '/usr/include/python2.7/numpy/')
    
    # FIXME: Supresses NumPy deprecation warnings. Needs to converted to the newer API.
    env.Append(CPPDEFINES = ['NPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION'])
    uniqueCheckLib(conf, pythonLib)
    env.AppendUnique(CPPPATH = [pythonIncPath, numpyIncPath])
    if not conf.CheckCXXHeader('Python.h'):
        errorMissingHeader('Python.h', 'Python')
    # Check for numpy header needs python header first to compile
    if not conf.CheckCXXHeader(['Python.h', 'numpy/arrayobject.h']):
        errorMissingHeader('numpy/arrayobject.h', 'Python NumPy')
else:
    buildpath += "-nopython"
    env.Append(CPPDEFINES = ['PRECICE_NO_PYTHON'])


# ====== GProf ======
if env["gprof"]:
    env.Append(CCFLAGS = ['-p', '-pg'])
    env.Append(LINKFLAGS = ['-p', '-pg'])
    buildpath += "-gprof"

# ====== Special Platforms ======
if env["platform"] == "supermuc":
    env.Append(CPPDEFINES = ['SuperMUC_WORK'])
elif env["platform"] == "hazelhen":
    env.Append(LINKFLAGS = ['-dynamic']) # Needed for correct linking against boost.log

print
env = conf.Finish() # Used to check libraries

#--------------------------------------------- Define sources and build targets

(sourcesAllNoMain, sourcesMain, sourcesTests) = SConscript (
    'src/SConscript-linux',
    variant_dir = buildpath,
    duplicate = 0
)

staticlib = env.StaticLibrary (
    target = buildpath + '/libprecice',
    source = [sourcesAllNoMain]
)
env.Alias("staticlib", staticlib)

solib = env.SharedLibrary (
    target = buildpath + '/libprecice',
    source = [sourcesAllNoMain]
)
env.Alias("solib", solib)

bin = env.Program (
    target = buildpath + '/binprecice',
    source = [sourcesAllNoMain,
              sourcesMain]
)
env.Alias("bin", bin)

tests = env.Program (
    target = buildpath + '/testprecice',
    source = [sourcesAllNoMain,
              sourcesTests]
)
env.Alias("tests", tests)

# Creates a symlink that always points to the latest build
symlink = env.Command(
    target = "Symlink",
    source = None,
    action = "ln -fns {0} {1}".format(os.path.split(buildpath)[-1], os.path.join(os.path.split(buildpath)[0], "last"))
)

Default(staticlib, bin, solib, tests, symlink)

AlwaysBuild(symlink)

print "Targets:   " + ", ".join([str(i) for i in BUILD_TARGETS])
print "Buildpath: " + buildpath
print
