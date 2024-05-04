# 
# This file is part of the iothpy library: python support for ioth.
# 
# Copyright (c) 2020-2024   Dario Mylonopoulos
#                           Lorenzo Liso
#                           Francesco Testa
# Virtualsquare team.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU General Public License 
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
#!/usr/bin/python3

from skbuild import setup

long_description = ""
with open("README.md", "r") as f:
    long_description = f.read()

setup (
       #setuptools options
       name = 'iothpy',
       version = '1.3.1',
       author = 'Dario Mylonopoulos',
       author_email = 'dmylos@yahoo.it',
       url = 'https://github.com/ramenguy99/iothpy',
       description = 'Python library for internet of threads',
       long_description = long_description,
       long_description_content_type="text/markdown",
       packages = ["iothpy"],
       classifiers = ["Operating System :: POSIX :: Linux"],
       python_requires='>=3.6',

       #skbuild options
       cmake_args= [],
       setup_requires=["cmake"]
       )

