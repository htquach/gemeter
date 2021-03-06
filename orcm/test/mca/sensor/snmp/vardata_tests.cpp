/*
 * Copyright (c) 2015-2016  Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "vardata_tests.h"


#define SAFE_OBJ_RELEASE(x) if(NULL!=x) OBJ_RELEASE(x); x=NULL

#define NOOP_JOBID -999

using namespace std;

/*
 * Constructor tests
 */
TEST_F(ut_vardata_constructor_tests, test_string) {
    string s = string("Hello, World!");
    vardata probe = vardata(s);
    ASSERT_EQ(OPAL_STRING, probe.getDataType());
    ASSERT_EQ(0, s.compare(probe.getValue<string>()));
}

TEST_F(ut_vardata_constructor_tests, test_c_string) {
    char s[] = "Hello, World!";
    vardata probe = vardata(s);
    ASSERT_EQ(OPAL_STRING, probe.getDataType());
    ASSERT_EQ(0, strcmp(s,probe.getValue<char*>()));
}

TEST_F(ut_vardata_constructor_tests, test_float) {
    const float f = 3.1416;
    vardata probe = vardata(f);
    ASSERT_EQ(OPAL_FLOAT, probe.getDataType());
    ASSERT_TRUE( f == probe.getValue<float>());
}

TEST_F(ut_vardata_constructor_tests, test_double) {
    const double d = 3.14159265;
    vardata probe = vardata(d);
    ASSERT_EQ(OPAL_DOUBLE, probe.getDataType());
    ASSERT_TRUE( d == probe.getValue<double>());
}

TEST_F(ut_vardata_constructor_tests, test_int32_t) {
    const int32_t i = 31416;
    vardata probe = vardata(i);
    ASSERT_EQ(OPAL_INT32, probe.getDataType());
    ASSERT_TRUE( i == probe.getValue<int32_t>());
}

TEST_F(ut_vardata_constructor_tests, test_int64_t) {
    const int64_t i = 314159265;
    vardata probe = vardata(i);
    ASSERT_EQ(OPAL_INT64, probe.getDataType());
    ASSERT_TRUE( i == probe.getValue<int64_t>());
}

TEST_F(ut_vardata_constructor_tests, test_timestamp) {
    ASSERT_TRUE(true);
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    vardata probe = vardata(current_time);
    ASSERT_EQ(OPAL_TIMEVAL, probe.getDataType());

    struct timeval tv =  probe.getValue<struct timeval>();
    ASSERT_EQ( current_time.tv_sec, tv.tv_sec );
    ASSERT_EQ( current_time.tv_usec, tv.tv_usec );
}

/*
 * Method tests
 */
TEST_F(ut_vardata_tests, test_vardata_packto) {
    int probeValue;
    int32_t n = 1;
    char* probeString;

    opal_buffer_t buffer;
    OBJ_CONSTRUCT(&buffer, opal_buffer_t);

    testData->packTo(&buffer);
    testDataString->packTo(&buffer);

    ASSERT_EQ(testData->getValue<int32_t>(), fromOpalBuffer(&buffer).getValue<int32_t>());
    ASSERT_EQ(testDataString->getValue<string>(), fromOpalBuffer(&buffer).getValue<string>());

    OBJ_DESTRUCT(&buffer);
}

TEST_F(ut_vardata_tests, test_vardata_getDataPtr) {
    ASSERT_TRUE( NULL != testData->getDataPtr() );
}

/*
 * vardataList tests
 */
TEST_F(ut_vardataList_tests, test_pack_unpack) {
    packDataToBuffer(probeList, &buffer);

    vector<vardata> testList = unpackDataFromBuffer(&buffer);

    ASSERT_EQ(probeList.size(), testList.size());
    for (int i = 0; i < probeList.size(); ++i) {
        ASSERT_EQ(probeList[i].getDataType(), testList[i].getDataType());
        if (OPAL_STRING == probeList[i].getDataType()) {
            ASSERT_EQ(0, probeList[i].getValue<string>().compare(testList[i].getValue<string>()));
        } else {
            switch(probeList[i].getDataType()) {
                case OPAL_FLOAT:
                    ASSERT_FLOAT_EQ(probeList[i].getValue<float>(), testList[i].getValue<float>());
                    break;
                case OPAL_DOUBLE:
                    ASSERT_DOUBLE_EQ(probeList[i].getValue<double>(), testList[i].getValue<double>());
                    break;
                case OPAL_INT32:
                    ASSERT_EQ(probeList[i].getValue<int32_t>(), testList[i].getValue<int32_t>());
                    break;
                case OPAL_INT64:
                    ASSERT_EQ(probeList[i].getValue<int64_t>(), testList[i].getValue<int64_t>());
                    break;
            }
        }
    }
}

TEST_F(ut_vardata_negative, packTo) {
    ASSERT_THROW( (new vardata((int) 1))->packTo(NULL), unableToPack );
}
