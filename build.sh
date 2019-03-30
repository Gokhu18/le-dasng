#! /bin/bash
function nl_error {
  echo "build.sh: $*" >&2
  exit 1
}
function msg {
  echo "build.sh: $*" >&2
}

function print_usage {
cat  <<EOF
$0 [cross] [install]
EOF
}

if [ "$1" = "--help" -o "$1" = "-h" ]; then
  print_usage
  exit 0
fi

if [ "$1" = "cross" ]; then
  shift
  crname='-cross'
  crargs=" -DCMAKE_TOOLCHAIN_FILE=arm-toolchain.cmake"
else
  crname=''
  crargs=''
fi

if [ "$1" = "install" ]; then
  shift
  do_install="install"
else
  do_install=""
fi

if [ -n "$1" ]; then
  msg "Unrecognized option: '$1'"
  print_usage
  exit 0
fi

subdir=''
relsrcroot='../git'
if [ ! -d .git ]; then
  # Not running in the source root
  while [ `pwd` != '/' -a ! -d .git ]; do
    subdir="/`basename $PWD`$subdir"
    relsrcroot="../$relsrcroot"
    cd ..
  done
  [ -d .git -a ../git -ef . ] || nl_error Unable to locate source root
  [ -f le-dasng-doxygen.css ] || nl_error Not in the correct source tree
fi
branch=`git rev-parse --abbrev-ref HEAD`
builddir="../build$crname-$branch$subdir"
if [ ! -d $builddir ]; then
  msg Creating build $builddir
  mkdir -p $builddir
  cd $builddir
  cmake -DCMAKE_BUILD_TYPE=Debug $crargs $relsrcroot$subdir && make $do_install
else
  msg Using existing $builddir
  cd $builddir
  make $do_install
fi
