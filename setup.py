#!/usr/bin/python3

from skbuild import setup

long_description = ""
with open("README.md", "r") as f:
    long_description = f.read()

setup (
       #setuptools options
       name = 'pycoxnet',
       version = '1.1.1',
       author = 'Dario Mylonopoulos',
       author_email = 'dmylos@yahoo.it',
       url = 'https://docs.python.org/extending/building',
       description = 'This is a demo package for pycoxnet',
       long_description = long_description,
       long_description_content_type="text/markdown",
       packages = ["pycoxnet"],
       classifiers = ["Operating System :: POSIX :: Linux"],
       python_requires='>=3.6',

       #skbuild options
       cmake_args= [],
       setup_requires=["cmake"]
       )

