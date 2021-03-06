//
// Copyright 2010-2011 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <boost/test/unit_test.hpp>
#include <uhd/types/time_spec.hpp>
#include <boost/foreach.hpp>
#include <boost/thread.hpp> //sleep
#include <iostream>

BOOST_AUTO_TEST_CASE(test_time_spec_compare){
    std::cout << "Testing time specification compare..." << std::endl;

    BOOST_CHECK(uhd::time_spec_t(2.0) == uhd::time_spec_t(2.0));
    BOOST_CHECK(uhd::time_spec_t(2.0) > uhd::time_spec_t(1.0));
    BOOST_CHECK(uhd::time_spec_t(1.0) < uhd::time_spec_t(2.0));

    BOOST_CHECK(uhd::time_spec_t(1.1) == uhd::time_spec_t(1.1));
    BOOST_CHECK(uhd::time_spec_t(1.1) > uhd::time_spec_t(1.0));
    BOOST_CHECK(uhd::time_spec_t(1.0) < uhd::time_spec_t(1.1));

    BOOST_CHECK(uhd::time_spec_t(0.1) == uhd::time_spec_t(0.1));
    BOOST_CHECK(uhd::time_spec_t(0.2) > uhd::time_spec_t(0.1));
    BOOST_CHECK(uhd::time_spec_t(0.1) < uhd::time_spec_t(0.2));
}

#define CHECK_TS_EQUAL(lhs, rhs) \
    BOOST_CHECK_CLOSE((lhs).get_real_secs(), (rhs).get_real_secs(), 0.001)

BOOST_AUTO_TEST_CASE(test_time_spec_arithmetic){
    std::cout << "Testing time specification arithmetic..." << std::endl;

    CHECK_TS_EQUAL(uhd::time_spec_t(2.3) + uhd::time_spec_t(1.0), uhd::time_spec_t(3.3));
    CHECK_TS_EQUAL(uhd::time_spec_t(2.3) - uhd::time_spec_t(1.0), uhd::time_spec_t(1.3));
    CHECK_TS_EQUAL(uhd::time_spec_t(1.0) + uhd::time_spec_t(2.3), uhd::time_spec_t(3.3));
    CHECK_TS_EQUAL(uhd::time_spec_t(1.0) - uhd::time_spec_t(2.3), uhd::time_spec_t(-1.3));
}

BOOST_AUTO_TEST_CASE(test_time_spec_parts){
    std::cout << "Testing time specification parts..." << std::endl;

    BOOST_CHECK_EQUAL(uhd::time_spec_t(1.1).get_full_secs(), 1);
    BOOST_CHECK_CLOSE(uhd::time_spec_t(1.1).get_frac_secs(), 0.1, 0.001);
    BOOST_CHECK_EQUAL(uhd::time_spec_t(1.1).get_tick_count(100), 10);

    BOOST_CHECK_EQUAL(uhd::time_spec_t(-1.1).get_full_secs(), -2);
    BOOST_CHECK_CLOSE(uhd::time_spec_t(-1.1).get_frac_secs(), 0.9, 0.001);
    BOOST_CHECK_EQUAL(uhd::time_spec_t(-1.1).get_tick_count(100), 90);
}

BOOST_AUTO_TEST_CASE(test_time_spec_get_system_time){
    std::cout << "Testing time specification get system time..." << std::endl;

    //Not really checking for high resolution timing here,
    //just need to check that system time is minimally working.

    uhd::time_spec_t start = uhd::time_spec_t::get_system_time();
    boost::this_thread::sleep(boost::posix_time::milliseconds(500));
    uhd::time_spec_t stop = uhd::time_spec_t::get_system_time();

    uhd::time_spec_t diff = stop - start;
    std::cout << "start: " << start.get_real_secs() << std::endl;
    std::cout << "stop: " << stop.get_real_secs() << std::endl;
    std::cout << "diff: " << diff.get_real_secs() << std::endl;
    BOOST_CHECK(diff.get_real_secs() > 0); //assert positive
    BOOST_CHECK(diff.get_real_secs() < 1.0); //assert under 1s
}

BOOST_AUTO_TEST_CASE(test_time_spec_neg_values){
    uhd::time_spec_t ts1(0.3);
    uhd::time_spec_t ts2(1, -0.9);
    std::cout << "ts1 " << ts1.get_real_secs() << std::endl;
    std::cout << "ts2 " << ts2.get_real_secs() << std::endl;
    BOOST_CHECK(ts1 > ts2);

    uhd::time_spec_t tsa(430.001083);
    uhd::time_spec_t tsb(429.999818);
    uhd::time_spec_t tsc(0.3);
    uhd::time_spec_t tsd = tsa - tsb;
    std::cout << "tsa " << tsa.get_real_secs() << std::endl;
    std::cout << "tsb " << tsb.get_real_secs() << std::endl;
    std::cout << "tsc " << tsc.get_real_secs() << std::endl;
    std::cout << "tsd " << tsd.get_real_secs() << std::endl;
    BOOST_CHECK(tsa > tsb);
    BOOST_CHECK(tsc > tsd);
}
