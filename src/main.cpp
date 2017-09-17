#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
    auto found_null = s.find("null");
    auto b1 = s.find_first_of("[");
    auto b2 = s.rfind("}]");
    if (found_null != string::npos) {
        return "";
    } else if (b1 != string::npos && b2 != string::npos) {
        return s.substr(b1, b2 - b1 + 2);
    }
    return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
    double result = 0.0;
    for (int i = 0; i < coeffs.size(); i++) {
        result += coeffs[i] * pow(x, i);
    }
    return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
    assert(xvals.size() == yvals.size());
    assert(order >= 1 && order <= xvals.size() - 1);
    Eigen::MatrixXd A(xvals.size(), order + 1);
    
    for (int i = 0; i < xvals.size(); i++) {
        A(i, 0) = 1.0;
    }
    
    for (int j = 0; j < xvals.size(); j++) {
        for (int i = 0; i < order; i++) {
            A(j, i + 1) = A(j, i) * xvals(j);
        }
    }
    
    auto Q = A.householderQr();
    auto result = Q.solve(yvals);
    return result;
}

Eigen::MatrixXd convertToCoordinates(double x, double y, double psi, const vector<double> & ptsx, const vector<double> & ptsy) {
    
    assert(ptsx.size() == ptsy.size());
    unsigned len = ptsx.size();
    
    auto pathpoints = Eigen::MatrixXd(2,len);
    
    for (auto i=0; i<len ; ++i){
        pathpoints(0,i) =   cos(psi) * (ptsx[i] - x) + sin(psi) * (ptsy[i] - y);
        pathpoints(1,i) =  -sin(psi) * (ptsx[i] - x) + cos(psi) * (ptsy[i] - y);
    }
    
    return pathpoints;
}

int main() {
    uWS::Hub h;
    
    // MPC is initialized here!
    MPC mpc = MPC();
    
    h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                       uWS::OpCode opCode) {
        // "42" at the start of the message means there's a websocket message event.
        // The 4 signifies a websocket message
        // The 2 signifies a websocket event
        string sdata = string(data).substr(0, length);
        cout << sdata << endl;
        if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
            string s = hasData(sdata);
            if (s != "") {
                auto j = json::parse(s);
                string event = j[0].get<string>();
                if (event == "telemetry") {
                    // j[1] is the data JSON object
                    vector<double> ptsx = j[1]["ptsx"];
                    vector<double> ptsy = j[1]["ptsy"];
                    double px = j[1]["x"];
                    double py = j[1]["y"];
                    double psi = j[1]["psi"];
                    double v = j[1]["speed"];
                    double steering_angle = j[1]["steering_angle"];
                    double throttle = j[1]["throttle"];
                    
                    cout << "px: " << px << endl;
                    cout << "py: " << py << endl;
                    cout << "psi: " << psi << endl;
                    cout << "v: " << v << endl;
                    cout << "steering_angle: " << steering_angle << endl;
                    cout << "throttle: " << throttle << endl;
                    
                    /*
                     * Calculate steering angle and throttle using MPC.
                     * Both are in between [-1, 1].
                     */
                    
                    Eigen::MatrixXd pathpoints = convertToCoordinates(px, py, psi, ptsx, ptsy);
                    Eigen::VectorXd xvals = pathpoints.row(0);
                    Eigen::VectorXd yvals = pathpoints.row(1);
                    
                    auto coeffs = polyfit(xvals, yvals, 3);
                    
                    // calculate the cross track error
                    double cte = coeffs[0];
                    
                    // calculate the orientation error
                    double epsi = -atan(coeffs[1]);
                    
                    double dt = mpc.getTimeInterval();
                    double Lf = 2.67;
                    
                    // use kinematic model to predict the state in time interval dt.
                    double px_actual = v * dt;
                    double py_actual = 0;
                    double psi_actual = - v * steering_angle * dt / Lf;
                    double v_actual = v + throttle * dt;
                    double cte_actual = cte + v * sin(epsi) * dt;
                    double epsi_actual = epsi + psi_actual;
                    
                    // state in vehicle coordinates
                    Eigen::VectorXd state(6);
                    state << px_actual, py_actual, psi_actual, v_actual, cte_actual, epsi_actual;
                    for (int i=0; i<state.size(); i++) {
                        cout << "state[" << i << "]: " << state[i] << endl;
                    }
                    
                    auto solution = mpc.Solve(state, coeffs);
                    
                    double steer_value = solution[6];
                    double throttle_value = solution[7];
                    
                    json msgJson;
                    // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
                    // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
                    steer_value = steer_value/deg2rad(25);
                    throttle_value = fmod(throttle_value, 1.0);
                    
                    msgJson["steering_angle"] = -steer_value;
                    msgJson["throttle"] = throttle_value;
                    
                    //Display the MPC predicted trajectory
                    vector<double> mpc_x_vals;
                    vector<double> mpc_y_vals;
                    
                    mpc_x_vals.push_back(solution[0]);
                    mpc_y_vals.push_back(solution[1]);
                    
                    //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
                    // the points in the simulator are connected by a Green line
                    
                    msgJson["mpc_x"] = mpc_x_vals;
                    msgJson["mpc_y"] = mpc_y_vals;
                    
                    //Display the waypoints/reference line
                    vector<double> next_x_vals;
                    vector<double> next_y_vals;
                    
                    //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
                    // the points in the simulator are connected by a Yellow line
                    
                    msgJson["next_x"] = next_x_vals;
                    msgJson["next_y"] = next_y_vals;
                    
                    
                    auto msg = "42[\"steer\"," + msgJson.dump() + "]";
                    std::cout << msg << std::endl;
                    // Latency
                    // The purpose is to mimic real driving conditions where
                    // the car does actuate the commands instantly.
                    //
                    // Feel free to play around with this value but should be to drive
                    // around the track with 100ms latency.
                    //
                    // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
                    // SUBMITTING.
                    this_thread::sleep_for(chrono::milliseconds(100));
                    ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
                }
            } else {
                // Manual driving
                std::string msg = "42[\"manual\",{}]";
                ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
            }
        }
    });
    
    // We don't need this since we're not using HTTP but if it's removed the
    // program
    // doesn't compile :-(
    h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                       size_t, size_t) {
        const std::string s = "<h1>Hello world!</h1>";
        if (req.getUrl().valueLength == 1) {
            res->end(s.data(), s.length());
        } else {
            // i guess this should be done more gracefully?
            res->end(nullptr, 0);
        }
    });
    
    h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
        std::cout << "Connected!!!" << std::endl;
    });
    
    h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                           char *message, size_t length) {
        ws.close();
        std::cout << "Disconnected" << std::endl;
    });
    
    int port = 4567;
    if (h.listen(port)) {
        std::cout << "Listening to port " << port << std::endl;
    } else {
        std::cerr << "Failed to listen to port" << std::endl;
        return -1;
    }
    h.run();
}
