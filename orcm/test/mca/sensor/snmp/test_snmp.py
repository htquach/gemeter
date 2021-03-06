#!/usr/bin/python

#
# Copyright (c) 2015  Intel, Inc. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

import re
import sys
import subprocess
import os.path
from os import getenv
from subprocess import Popen, PIPE, STDOUT

cf_name="snmp.conf"

def get_gtests(gtest_binary):
    process = Popen([ gtest_binary, '--gtest_list_tests' ], stdout=PIPE)
    (output, err) = process.communicate()
    exit_code = process.wait()
    if exit_code != 0:
        return []
    lines=output.split('\n')
    fixture=''
    c=0
    tests=[]
    for line in lines:
        c=c+1
        if c!=1:
            if line == '':
                continue
            if line[0] != ' ':
                fixture=line
            else:
                test=fixture+line[2:]
                tests.append(test)
    return tests

def execute_test(gtest_binary, test_name):
    process = Popen([ gtest_binary, "--gtest_color=yes", "--gtest_filter=%s"%(test_name) ], stdout=PIPE, stderr=PIPE)
    (output, err) = process.communicate()
    rv=process.wait()
    return (rv, output, err)

def prefix_search():
    prefix="/usr/local1/"
    filename=os.getcwd()+"/../../../../../config.status"
    pattern = re.compile(r'S\[\"prefix\"\]=\"(.*)\"')
    with open(filename) as f:
        for line in f:
            match = pattern.search(line)
            if match:
                prefix = match.group(1)

    return prefix

def setup_tests():
    prefix = prefix_search()
    cf_src = getenv("srcdir")
    if cf_src:
        cf_src += "/test_files/snmp.conf"
    else:
        cf_src = "test_files/snmp.conf"
    cf_dst=prefix + "/etc/" + cf_name
    isThere = os.path.isfile(cf_dst)
    if isThere == False:
        os.system("sudo cp %s %s" % (cf_src, cf_dst))

def run_tests(gtest_binary):
    setup_tests()
    tests=get_gtests(gtest_binary)
    ret = 0
    passed=0
    failed=0
    print '+RUNNING: %s'%gtest_binary
    for test in tests:
        (rv,output, err)=execute_test(gtest_binary, test)
        if rv != 0:
            print '+STDERR:\n%s'%err
            print '+STDOUT:\n%s'%output
            ret=rv
            failed=failed+1
        else:
            passed=passed+1
    print '+TEST SUMMARY PASSED = %d'%(passed)
    print '+TEST SUMMARY FAILED = %d'%(failed)
    return ret

prog='./snmp_tests'
sys.exit(run_tests(prog))
