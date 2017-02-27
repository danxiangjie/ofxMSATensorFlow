/*
Generative handwriting with a Long Short-Term Memory (LSTM) Recurrent Mixture Density Network (RMDN),
ala [Graves2013](https://arxiv.org/abs/1308.0850)

Brilliant tutorial on inner workings [here](http://blog.otoro.net/2015/12/12/handwriting-generation-demo-in-tensorflow/),
which also provides the base for the training code.

Models are trained and saved in python with this code (https://github.com/memo/write-rnn-tensorflow),
and loaded in openframeworks for prediction.
Given a sequence of points, the model predicts the position for the next point and pen-up probability.

I'm supplying a model pretrained on the [IAM online handwriting dataset](http://www.fki.inf.unibe.ch/databases/iam-on-line-handwriting-database)

Note that this demo does not do handwriting *synthesis*, i.e. text to handwriting ala [Graves' original demo](https://www.cs.toronto.edu/~graves/handwriting.html)
It just does *asemic* handwriting, producing sguiggles that are statistically similar to the training data,
e.g. same kinds of slants, curvatures, sharpnesses etc., but not nessecarily legible.

There is an implementation (and great tutorial) of *synthesis* using attention [here](https://greydanus.github.io/2016/08/21/handwriting/)
which I am also currently converting to work in openframeworks.
This attention-based synthesis implementation is also based on [Graves2013](https://arxiv.org/abs/1308.0850),
which I highly recommend to anyone really interested in understanding generative RNNs
 */

#include "ofMain.h"
#include "ofxMSATensorFlow.h"


//--------------------------------------------------------------
// sample a point from a bivariate gaussian mixture model
// from http://www.statisticalengineering.com/bivariate_normal.htm
ofVec2f sample_from_bi_gmm(std::default_random_engine& rng,// random number generator
                           const vector<float>& o_pi,      // vector of mixture weights
                           const vector<float>& o_mu1,     // means 1
                           const vector<float>& o_mu2,     // means 2
                           const vector<float>& o_sigma1,  // sigmas 1
                           const vector<float>& o_sigma2,  // sigmas 2
                           const vector<float>& o_corr     // correlations
                           ) {

    ofVec2f ret;

    int k = o_pi.size();
    if(k == 0 || o_mu1.size() != k || o_mu2.size() != k || o_sigma1.size() != k || o_sigma2.size() != k || o_corr.size() != k) {
        ofLogWarning() << " sample_from_bi_gmm vector size mismatch ";
        return ret;
    }

    // pick mixture component to sample from
    int i = msa::tf::sample_from_prob(rng, o_pi);

    // sample i'th bivariate gaussian
    double mu1 = o_mu1[i];
    double mu2 = o_mu2[i];
    double sigma1 = o_sigma1[i];
    double sigma2 = o_sigma2[i];
    double rho = o_corr[i];

    // two independent zero mean, unit variance gaussian variables
    std::normal_distribution<double> gaussian(0.0, 1.0);

    double z1 = gaussian(rng);
    double z2 = gaussian(rng);

    ret.x = mu1 + sigma1 * z1;
    ret.y = mu2 + sigma2 * (z1*rho + z2*sqrt(1-rho*rho));

    return ret;
}


//--------------------------------------------------------------
// visualise bivariate gaussian distribution as an ellipse
// rotate unit circle by matrix of normalised eigenvectors and scale by sqrt eigenvalues (tip from @colormotor)
// based on http://www.math.harvard.edu/archive/21b_fall_04/exhibits/2dmatrices/index.html
void draw_bi_gaussian(float mu1,     // mean 1
                      float mu2,     // mean 2
                      float sigma1,  // sigma 1
                      float sigma2,  // sigma 2
                      float rho,     // correlation
                      float scale=1.0 // arbitrary scale
        ) {
    // Correlation Matrix [[a b], [c d]]
    double a = sigma1*sigma1;
    double b = rho*sigma1*sigma2;
    double c = b;
    double d = sigma2*sigma2;

    double T = a+d; // trace
    double D = (a*d)-(b*c); // determinant

    // eigenvalues
    double l1 = T/2. + T*sqrt( 1./(4.-D) );
    double l2 = T/2. - T*sqrt( 1./(4.-D) );

    ofVec2f v1 = ofVec2f(b, l1-a).normalized();
    ofVec2f v2 = ofVec2f(b, l2-a).normalized();

    // create 4x4 transformation matrix for a unit circle
    // eigenvectors in upper left corner for rotation around z
    // scale diagonal by eigenvalues
    ofMatrix4x4 m44;
    m44.ofMatrix4x4::makeIdentityMatrix();
    m44.getPtr()[0] = v1.x * sqrtf(fabsf(l1)) * scale;
    m44.getPtr()[1] = v1.y;
    m44.getPtr()[4] = v2.x;
    m44.getPtr()[5] = v2.y * sqrtf(fabsf(l2)) * scale;
    m44.setTranslation(mu1, mu2, 0);

    ofPushMatrix();
    ofMultMatrix(m44);
    ofDrawCircle(0, 0, 1);
    ofPopMatrix();

    //     trying raw opengl commands instead of ofXXX to make sure column and row order stuff is done as I want :S
    //        glPushMatrix();
    //        glMultMatrixf(m44.getPtr());
    //        ofDrawCircle(0, 0, 1);
    //        glPopMatrix();
}



//--------------------------------------------------------------
// visualise bivariate gaussian mixture model
void draw_bi_gmm(const vector<float>& o_pi,      // vector of mixture weights
                 const vector<float>& o_mu1,     // means 1
                 const vector<float>& o_mu2,     // means 2
                 const vector<float>& o_sigma1,  // sigmas 1
                 const vector<float>& o_sigma2,  // sigmas 2
                 const vector<float>& o_corr,    // correlations
                 const ofVec2f& offset=ofVec2f(0, 0),
                 float draw_scale=1.0,
                 float gaussian_scale=1.0,
                 ofColor color_min=ofColor(255, 0, 0, 10),
                 ofColor color_max=ofColor(255, 0, 0, 50)

        ) {

    int k = o_pi.size();
    if(k == 0 || o_mu1.size() != k || o_mu2.size() != k || o_sigma1.size() != k || o_sigma2.size() != k || o_corr.size() != k) {
        ofLogWarning() << " draw_gmm vector size mismatch ";
        return;
    }

    ofPushMatrix();
    ofTranslate(offset);
    ofScale(draw_scale, draw_scale);
    for(int i=0; i<k; i++) {
        ofColor c(color_min);
        c.lerp(color_max, o_pi[i]);
        ofSetColor(c);
        draw_bi_gaussian(o_mu1[i], o_mu2[i], o_sigma1[i], o_sigma2[i], o_corr[i], gaussian_scale);
    }
    ofPopMatrix();
}


//--------------------------------------------------------------
//--------------------------------------------------------------
class ofApp : public ofBaseApp {
public:

    // shared pointer to tensorflow::Session
    msa::tf::Session_ptr session;


    // data in and out of model
    tensorflow::Tensor t_data_in;   // data in
    tensorflow::Tensor t_state;     // current lstm state
    vector<tensorflow::Tensor> t_out; // returned from session run [data_out_pi, data_out_mu1, data_out_mu2, data_out_sigma1, data_out_sigma2, data_out_corr, data_out_eos, state_out]

    // convert data in t_out (output of model) to more managable types
    // parameters of bivariate gaussian mixture model, and probability for end of stroke
    struct ProbData {
        vector<float> o_pi;      // contains all mixture weights (e.g. default 20 components)
        vector<float> o_mu1;     // " means 1
        vector<float> o_mu2;     // " means 2
        vector<float> o_sigma1;  // " sigmas 1
        vector<float> o_sigma2;  // " sigmas 2
        vector<float> o_corr;    // " correlations
        float o_eos;             // end of stroke probability
    };

    ProbData last_model_output;

    // all points. xy storing relative offset from prev pos, and z storing end of stroke (0: draw, 1: eos)
    vector<ofVec3f> pts;

    // probability parameters for all points (i.e. i'th probability is the probability distribution which the i'th point was sampled from)
    vector<ProbData> probs;


    // model file management
    string model_root_dir = "models";
    vector<string> model_names;
    int cur_model_index = 0;


    // random generator for sampling
    std::default_random_engine rng;


    // other vars
    int prime_length = 300;
    float draw_scale = 5;
    float gaussian_draw_scale = 1;
    ofVec2f draw_pos = ofVec2f(100, 50);

    bool do_auto_run = true;    // auto run every frame
    bool do_run_once = false;   // only run one frame


    //--------------------------------------------------------------
    void setup() {
        ofSetVerticalSync(true);
        ofSetFrameRate(60);
        ofSetLogLevel(OF_LOG_VERBOSE);

        // scan models dir
        ofDirectory dir;
        dir.listDir(model_root_dir);
        for(int i=0; i<dir.getFiles().size(); i++) model_names.push_back(dir.getName(i));
        sort(model_names.begin(), model_names.end());
        load_model_index(0);

        // seed rng
        rng.seed(ofGetSystemTimeMicros());
    }


    //--------------------------------------------------------------
    // Load graph (i.e. trained model and exported  from python) by folder index
    // and initialize session
    void load_model_index(int index) {
        cur_model_index = ofClamp(index, 0, model_names.size()-1);
        load_model(model_root_dir + "/" + model_names[cur_model_index]);
    }


    //--------------------------------------------------------------
    // Load graph (i.e. trained model and exported from python) by folder name
    // and initialize session
    void load_model(string dir) {
        // init session with graph
        session = msa::tf::create_session_with_graph(dir + "/graph_frz.pb");

        // init tensor for input
        // meeds to be 3 floats (x, y, end of stroke), BUT not a vector, but rank3 tensor with other dims 1.
        // WHY? because that's how the model was designed to make the internal calculations easier (batch size etc)
        // tbh the model could be redesigned to accept just a 3 element vector, and then internally shift up to 1x1x3
        t_data_in = tensorflow::Tensor(tensorflow::DT_FLOAT, {1, 1, 3});

        // prime model
        prime_model(pts, prime_length);
    }


    //--------------------------------------------------------------
    // prime model with string
    void prime_model(const vector<ofVec3f>& prime_data, int prime_length) {
        t_state = tensorflow::Tensor(); // reset initial state to use zeros
        for(int i=MAX(0, prime_data.size()-prime_length); i<prime_data.size(); i++) {
            run_model(prime_data[i], t_state);
        }

    }



    //--------------------------------------------------------------
    // run model with one data point
    void run_model(ofVec3f pt, const tensorflow::Tensor &state_in = tensorflow::Tensor()) {
        // format input data
        msa::tf::array_to_tensor(pt.getPtr(), t_data_in);

        // run graph, feed inputs, fetch output
        vector<string> fetch_tensors = { "data_out_pi", "data_out_mu1", "data_out_mu2", "data_out_sigma1", "data_out_sigma2", "data_out_corr", "data_out_eos", "state_out" };
        tensorflow::Status status;
        if(state_in.NumElements() > 0) {
            status = session->Run({ { "data_in", t_data_in }, { "state_in", state_in } }, fetch_tensors, {}, &t_out);
        } else {
            status = session->Run({ { "data_in", t_data_in }}, fetch_tensors, {}, &t_out);
        }

        if(status != tensorflow::Status::OK()) {
            ofLogError() << status.error_message();
        }

        if(t_out.size() > 1) {
            t_state = t_out.back();

            last_model_output.o_pi        = msa::tf::tensor_to_vector<float>(t_out[0]);
            last_model_output.o_mu1       = msa::tf::tensor_to_vector<float>(t_out[1]);
            last_model_output.o_mu2       = msa::tf::tensor_to_vector<float>(t_out[2]);
            last_model_output.o_sigma1    = msa::tf::tensor_to_vector<float>(t_out[3]);
            last_model_output.o_sigma2    = msa::tf::tensor_to_vector<float>(t_out[4]);
            last_model_output.o_corr      = msa::tf::tensor_to_vector<float>(t_out[5]);
            last_model_output.o_eos       = msa::tf::tensor_to_scalar<float>(t_out[6]);
        }
    }


    //--------------------------------------------------------------
    void draw() {
        draw_pos.y = ofGetHeight()/2;

        stringstream str;
        str << ofGetFrameRate() << endl;
        str << endl;
        str << "TAB   : auto run (" << do_auto_run << ")" << endl;
        str << "RIGHT : sample one pt " << endl;
        str << "LEFT  : delete last point " << endl;
        str << "DEL   : clear drawing " << endl;
        str << endl;

        str << "Press number key to load model: " << endl;
        for(int i=0; i<model_names.size(); i++) {
            auto marker = (i==cur_model_index) ? ">" : " ";
            str << " " << (i+1) << " : " << marker << " " << model_names[i] << endl;
        }

        str << endl;
        str << "Draw with mouse to prime the model" << endl;
        str << endl;


        if(session) {
            // sample 2d position from bivariate gaussian mixture model
            ofVec3f pt = sample_from_bi_gmm(rng, last_model_output.o_pi, last_model_output.o_mu1, last_model_output.o_mu2, last_model_output.o_sigma1, last_model_output.o_sigma2, last_model_output.o_corr);

            // end of stroke probability
            pt.z = ofRandomuf() < last_model_output.o_eos;

            if(do_auto_run || do_run_once) {
                if(do_run_once) do_run_once = false;

                // add sampled pt to drawing
                pts.push_back(pt);

                // add probabilities to vector
                probs.push_back(last_model_output);

                // feed sampled pt back into model
                run_model(pt, t_state);
            }
        }

        // draw stuff. remember pts stores *relative* offsets and not absolute positions
        ofVec3f cur_pos = draw_pos; // start drawing at draw_pos
        ofSetLineWidth(3);
        for(int i=0; i<pts.size(); i++) {
            ofVec2f next_pos = cur_pos + pts[i] * draw_scale;

            // draw probabilities
            // the way I'm doing this is rather inefficient, because every frame the matrices for each of the components for each of the points is reconstructed,
            // instead, this could be cached to save computation, but i'm trying to keep this a short, simple example
            const ProbData& p = probs[i];
            draw_bi_gmm(p.o_pi, p.o_mu1, p.o_mu2, p.o_sigma1, p.o_sigma2, p.o_corr, next_pos, draw_scale, gaussian_draw_scale);

            // draw points. xy is position, z is end of stroke
            if(i>0 && pts[i-1].z < 0.5) { //  need to check z of previous stroke to decide whether to draw or not
                ofSetColor(0);
                ofDrawLine(cur_pos, next_pos);
            }
//            ofCircle(next_pos, 2);    // draw pt

            cur_pos = next_pos;
        }


        // if writing goes off screen, clear drawing
        if(cur_pos.x < 0 || cur_pos.x > ofGetWidth() || cur_pos.y > ofGetHeight() || cur_pos.y < 0) {
            pts.clear();
            probs.clear();
        }


        // draw texts
        ofSetColor(100);
        ofDrawBitmapString(str.str(), 20, 20);
    }


    //--------------------------------------------------------------
    void keyPressed(int key) {
        switch(key) {
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            load_model_index(key-'1');
            break;

        case OF_KEY_DEL:
            pts.clear();
            probs.clear();
            break;

        case OF_KEY_TAB:
            do_auto_run ^= true;
            break;

        case OF_KEY_RIGHT:
            do_run_once = true;
            do_auto_run = false;
            break;

        case OF_KEY_LEFT:
            pts.pop_back();
            probs.pop_back();
            //            prime_model(pts, prime_length); // prime model on key release to avoid lockup if key is held down
            do_auto_run = false;
            ofClear(ofGetBackgroundColor());
            break;

        default:
            break;
        }
    }


    //--------------------------------------------------------------
    void keyReleased(int key) {
        switch(key) {
        case OF_KEY_LEFT:
            prime_model(pts, prime_length); // prime model on key release to avoid lockup if key is held down
            break;
        }

    }

};

//========================================================================
int main() {
    ofSetupOpenGL(1280, 720, OF_WINDOW);
    ofRunApp(new ofApp());
}
