//
// Copyright 2010 Ettus Research LLC
// Copyright 2012 Fairwaves LLC
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

#include "usrp_cal_utils.hpp"
#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/paths.hpp>
#include <uhd/utils/algorithm.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread/thread.hpp>
#include <boost/math/special_functions/round.hpp>
#include <iostream>
#include <complex>
#include <cmath>
#include <ctime>

namespace po = boost::program_options;

/***********************************************************************
 * Transmit thread
 **********************************************************************/
static void tx_thread(uhd::usrp::multi_usrp::sptr usrp, const double tx_wave_freq, const double tx_wave_ampl){
    uhd::set_thread_priority_safe();

    //create a transmit streamer
    uhd::stream_args_t stream_args("fc32"); //complex floats
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

    //setup variables and allocate buffer
    uhd::tx_metadata_t md;
    md.has_time_spec = false;
    std::vector<samp_type> buff(tx_stream->get_max_num_samps()*10);

    //values for the wave table lookup
    size_t index = 0;
    const double tx_rate = usrp->get_tx_rate();
    const size_t step = boost::math::iround(wave_table_len * tx_wave_freq/tx_rate);
    wave_table table(tx_wave_ampl);

    //fill buff and send until interrupted
    while (not boost::this_thread::interruption_requested()){
        for (size_t i = 0; i < buff.size(); i++){
            buff[i] = samp_type(0,0);// table(index += step);
        }
        tx_stream->send(&buff.front(), buff.size(), md);
    }

    //send a mini EOB packet
    md.end_of_burst = true;
    tx_stream->send("", 0, md);
}

/***********************************************************************
 * Tune RX and TX routine
 **********************************************************************/
static double tune_rx_and_tx(uhd::usrp::multi_usrp::sptr usrp, const double tx_lo_freq, const double rx_offset){
    //tune the transmitter with no cordic
    uhd::tune_request_t tx_tune_req(tx_lo_freq);
    tx_tune_req.dsp_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
    tx_tune_req.dsp_freq = 0;
    usrp->set_tx_freq(tx_tune_req);

    //tune the receiver
    usrp->set_rx_freq(uhd::tune_request_t(usrp->get_tx_freq(), rx_offset));
/*
    //wait for the LOs to become locked
    boost::this_thread::sleep(boost::posix_time::milliseconds(50));
    boost::system_time start = boost::get_system_time();
    while (not usrp->get_tx_sensor("lo_locked").to_bool() or not usrp->get_rx_sensor("lo_locked").to_bool()){
        if (boost::get_system_time() > start + boost::posix_time::milliseconds(100)){
            throw std::runtime_error("timed out waiting for TX and/or RX LO to lock");
        }
    }
*/
    return usrp->get_tx_freq();
}

/***********************************************************************
 * Main
 **********************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[]){
    std::string args;
    double tx_wave_freq, tx_wave_ampl, rx_offset;
    double freq_start, freq_stop, freq_step;
    size_t nsamps;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("verbose", "enable some verbose")
        ("debug_raw_data", "save raw captured signals to files")
        ("args", po::value<std::string>(&args)->default_value(""), "device address args [default = \"\"]")
        ("tx_wave_freq", po::value<double>(&tx_wave_freq)->default_value(50e3), "Transmit wave frequency in Hz")
        ("tx_wave_ampl", po::value<double>(&tx_wave_ampl)->default_value(0.7), "Transmit wave amplitude in counts")
        ("rx_offset", po::value<double>(&rx_offset)->default_value(1e6), "RX LO offset from the TX LO in Hz")
        ("freq_start", po::value<double>(&freq_start), "Frequency start in Hz (do not specify for default)")
        ("freq_stop", po::value<double>(&freq_stop), "Frequency stop in Hz (do not specify for default)")
        ("freq_step", po::value<double>(&freq_step)->default_value(default_freq_step), "Step size for LO sweep in Hz")
        ("nsamps", po::value<size_t>(&nsamps)->default_value(default_num_samps), "Samples per data capture")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    //print the help message
    if (vm.count("help")){
        std::cout << boost::format("USRP Generate TX DC Offset Calibration Table %s") % desc << std::endl;
        std::cout <<
            "This application measures leakage between RX and TX on an XCVR daughterboard to self-calibrate.\n"
            << std::endl;
        return ~0;
    }

    //create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    //set the antennas to cal
    if (not uhd::has(usrp->get_rx_antennas(), "CAL") or not uhd::has(usrp->get_tx_antennas(), "CAL")){
        throw std::runtime_error("This board does not have the CAL antenna option, cannot self-calibrate.");
    }
    usrp->set_rx_antenna("CAL");
    usrp->set_tx_antenna("CAL");

    //set optimum defaults
    set_optimum_defaults(usrp);

    //create a receive streamer
    uhd::stream_args_t stream_args("fc32"); //complex floats
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    //create a transmitter thread
    boost::thread_group threads;
    threads.create_thread(boost::bind(&tx_thread, usrp, tx_wave_freq, tx_wave_ampl));

    //re-usable buffer for samples
    std::vector<samp_type> buff;

    //store the results here
    std::vector<result_t> results;

    const uhd::fs_path tx_fe_path = "/mboards/0/dboards/A/tx_frontends/0";
    uhd::property<uint8_t> &dc_i_prop = usrp->get_device()->get_tree()->access<uint8_t>(tx_fe_path / "lms6002d/tx_dc_i/value");
    uhd::property<uint8_t> &dc_q_prop = usrp->get_device()->get_tree()->access<uint8_t>(tx_fe_path / "lms6002d/tx_dc_q/value");

    if (not vm.count("freq_start")) freq_start = usrp->get_tx_freq_range().start() + 50e6;
    if (not vm.count("freq_stop")) freq_stop = usrp->get_tx_freq_range().stop() - 50e6;

    for (double tx_lo_i = freq_start; tx_lo_i <= freq_stop; tx_lo_i += freq_step){
        const double tx_lo = tune_rx_and_tx(usrp, tx_lo_i, rx_offset);

        //frequency constants for this tune event
        const double actual_rx_rate = usrp->get_rx_rate();
        const double actual_tx_freq = usrp->get_tx_freq();
        const double actual_rx_freq = usrp->get_rx_freq();
        const double bb_dc_freq = actual_tx_freq - actual_rx_freq;
        if (vm.count("verbose")) printf("actual_rx_rate = %0.2f MHz\n", actual_rx_rate/1e6);
        if (vm.count("verbose")) printf("actual_tx_freq = %0.2f MHz\n", actual_tx_freq/1e6);
        if (vm.count("verbose")) printf("actual_rx_freq = %0.2f MHz\n", actual_rx_freq/1e6);
        if (vm.count("verbose")) printf("bb_dc_freq = %0.2f MHz\n", bb_dc_freq/1e6);

        //bounds and results from searching
        int dc_i_start, dc_i_stop, dc_i_step;
        int dc_q_start, dc_q_stop, dc_q_step;
        double lowest_offset;
        int best_dc_i = 128, best_dc_q = 128;

        //capture initial uncorrected value
        dc_i_prop.set(best_dc_i);
        dc_q_prop.set(best_dc_q);
        capture_samples(usrp, rx_stream, buff, nsamps);
        const double initial_dc_dbrms = compute_tone_dbrms(buff, bb_dc_freq/actual_rx_rate);
        lowest_offset = initial_dc_dbrms;
        if (vm.count("verbose")) printf("initial_dc_dbrms = %2.0f dB\n", initial_dc_dbrms);

        if (vm.count("debug_raw_data")) write_samples_to_file(buff, "initial_samples.dat");

        for (size_t i = 0; i < 10; i++){
            if (vm.count("verbose")) printf("  iteration %d\n", i);

            bool has_improvement = false;

            switch (i) {
            case 0:
                dc_i_start = 0;
                dc_i_stop  = 256;
                dc_q_start = 0;
                dc_q_stop  = 256;
                dc_i_step = 10;
                dc_q_step = 10;
                break;
            case 1:
                dc_i_start = best_dc_i - 15;
                dc_i_stop  = best_dc_i + 15;
                dc_q_start = best_dc_q - 15;
                dc_q_stop  = best_dc_q + 15;
                dc_i_step = 1;
                dc_q_step = 1;
                break;
            default:
                dc_i_start = best_dc_i - 3;
                dc_i_stop  = best_dc_i + 3;
                dc_q_start = best_dc_q - 3;
                dc_q_stop  = best_dc_q + 3;
                dc_i_step = 1;
                dc_q_step = 1;
                break;
            };

            if (vm.count("verbose")) printf("    I in [%d; %d) step %d Q = %d\n",
                                            dc_i_start, dc_i_stop, dc_i_step, best_dc_q);

            dc_q_prop.set(best_dc_q);
            for (int dc_i = dc_i_start; dc_i < dc_i_stop; dc_i += dc_i_step){
                if (vm.count("verbose")) printf("      dc_i = %d", dc_i);

                dc_i_prop.set(dc_i);

                //receive some samples
                capture_samples(usrp, rx_stream, buff, nsamps);

                const double dc_dbrms = compute_tone_dbrms(buff, bb_dc_freq/actual_rx_rate);
                if (vm.count("verbose")) printf("    dc_dbrms = %2.0f dB", dc_dbrms);

                if (dc_dbrms < lowest_offset){
                    lowest_offset = dc_dbrms;
                    best_dc_i = dc_i;
                    has_improvement = true;
                    if (vm.count("verbose")) printf("    *");
                    if (vm.count("debug_raw_data")) write_samples_to_file(buff, "best_samples.dat");
                }
                if (vm.count("verbose")) printf("\n");

            }

            if (vm.count("verbose")) printf("    I = %d Q in [%d; %d) step %d\n",
                                            best_dc_i, dc_q_start, dc_q_stop, dc_q_step);

            dc_i_prop.set(best_dc_i);
            for (int dc_q = dc_q_start; dc_q < dc_q_stop; dc_q += dc_q_step){
                if (vm.count("verbose")) printf("      dc_q = %d", dc_q);

                dc_q_prop.set(dc_q);

                //receive some samples
                capture_samples(usrp, rx_stream, buff, nsamps);

                const double dc_dbrms = compute_tone_dbrms(buff, bb_dc_freq/actual_rx_rate);
                if (vm.count("verbose")) printf("    dc_dbrms = %2.0f dB", dc_dbrms);

                if (dc_dbrms < lowest_offset){
                    lowest_offset = dc_dbrms;
                    best_dc_q = dc_q;
                    has_improvement = true;
                    if (vm.count("verbose")) printf("    *");
                    if (vm.count("debug_raw_data")) write_samples_to_file(buff, "best_samples.dat");
                }
                if (vm.count("verbose")) printf("\n");

            }

            // Stop iterating if no imprevement, but do at least 3 iterations
            if (!has_improvement && i>1) break;
        }

        if (vm.count("verbose")) printf("  best_dc_i = %d best_dc_q = %d", best_dc_i, best_dc_q);
        if (vm.count("verbose")) printf("  lowest_offset = %2.0f dB  delta = %2.0f dB\n", lowest_offset, initial_dc_dbrms - lowest_offset);

        if (lowest_offset < initial_dc_dbrms){ //most likely valid, keep result
            result_t result;
            result.freq = tx_lo;
            result.real_corr = best_dc_i;
            result.imag_corr = best_dc_q;
            result.best = lowest_offset;
            result.delta = initial_dc_dbrms - lowest_offset;
            results.push_back(result);
            if (vm.count("verbose")){
                std::cout << boost::format("TX DC: %f MHz: lowest offset %f dB, corrected %f dB") % (tx_lo/1e6) % result.best % result.delta << std::endl;
            }
            else std::cout << "." << std::flush;
        }

    }
    std::cout << std::endl;

    //stop the transmitter
    threads.interrupt_all();
    threads.join_all();

    store_results(usrp, results, "TX", "tx", "lms_dc");

    return 0;
}
