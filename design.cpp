#include <iostream>
#include <cstring>
#include <cmath>
#include "GL/glut.h"

using namespace std;

int* current_signal = NULL;
int signal_length = 0;
char signal_title[100] = "";
bool is_manchester_encoding = false;

// Line Coding Functions

void encode_NRZ_L(char* data, int* output, int len) {
    for (int i = 0; i < len; i++)
        output[i] = (data[i] == '1') ? 1 : -1;
}

void encode_NRZ_I(char* data, int* output, int len) {
    int current_level = -1;
    for (int i = 0; i < len; i++) {
        if (data[i] == '1') current_level = -current_level;
        output[i] = current_level;
    }
}

void encode_MANCHESTER(char* data, int* output, int len) {
    for (int i = 0; i < len; i++) {
        if (data[i] == '0') {
            output[2*i] = 1;
            output[2*i + 1] = -1;
        } else {
            output[2*i] = -1;
            output[2*i + 1] = 1;
        }
    }
}

void encode_DIFF_MANCHESTER(char* data, int* output, int len) {
    int last_level = -1;
    for (int i = 0; i < len; i++) {
        if (data[i] == '0') {
            last_level = -last_level;
            output[2*i] = last_level;
            output[2*i + 1] = -last_level;
        } else {
            output[2*i] = last_level;
            output[2*i + 1] = -last_level;
        }
        last_level = -last_level;
    }
}

void encode_AMI(char* data, int* output, int len) {
    int last_polarity = 1;
    for (int i = 0; i < len; i++) {
        if (data[i] == '0') output[i] = 0;
        else {
            output[i] = last_polarity;
            last_polarity = -last_polarity;
        }
    }
}

// Scrambling

void apply_B8ZS(int* signal, int len) {
    int last_polarity = 1;
    for (int i = 0; i <= len - 8; i++) {
        if (signal[i] != 0) last_polarity = signal[i];
        bool eight_zeros = true;
        for (int j = i; j < i + 8; j++) {
            if (signal[j] != 0) { eight_zeros = false; break; }
        }
        if (eight_zeros) {
            signal[i+3] = last_polarity;
            signal[i+4] = -last_polarity;
            signal[i+6] = last_polarity;
            signal[i+7] = -last_polarity;
            last_polarity = -last_polarity;
            i += 7;
        }
    }
}

void apply_HDB3(int* signal, int len) {
    int ones = 0;
    int last_polarity = 1;
    for (int i = 0; i <= len - 4; i++) {
        if (signal[i] != 0) {
            last_polarity = signal[i];
            ones++;
        }
        bool four_zeros = true;
        for (int j = i; j < i + 4; j++) {
            if (signal[j] != 0) { four_zeros = false; break; }
        }
        if (four_zeros) {
            if (ones % 2 == 0) {
                signal[i] = -last_polarity;
                signal[i+3] = last_polarity;
            } else {
                signal[i+3] = last_polarity;
            }
            last_polarity = -last_polarity;
            ones = 1;
            i += 3;
        }
    }
}

// Modulation

int pcm_encode(double* analog, int samples, char* output, int bits) {
    double max_val = analog[0], min_val = analog[0];
    for (int i = 1; i < samples; i++) {
        if (analog[i] > max_val) max_val = analog[i];
        if (analog[i] < min_val) min_val = analog[i];
    }

    int levels = pow(2, bits);
    double step = (max_val - min_val) / levels;
    int out_len = 0;

    for (int i = 0; i < samples; i++) {
        int q = (int)((analog[i] - min_val) / step);
        if (q >= levels) q = levels - 1;
        for (int j = bits - 1; j >= 0; j--)
            output[out_len++] = ((q >> j) & 1) ? '1' : '0';
    }
    output[out_len] = '\0';
    return out_len;
}

int delta_modulation(double* analog, int samples, char* output) {
    double prediction = 0.0, delta = 0.5;
    for (int i = 0; i < samples; i++) {
        if (analog[i] > prediction) { output[i] = '1'; prediction += delta; }
        else { output[i] = '0'; prediction -= delta; }
    }
    output[samples] = '\0';
    return samples;
}

// Analytical Functions

void find_longest_palindrome(char* data, int len) {
    int max_len = 1, start = 0;
    for (int i = 0; i < len; i++) {
        for (int j = i; j < len; j++) {
            bool pal = true;
            for (int k = 0; k < (j - i + 1)/2; k++)
                if (data[i+k] != data[j-k]) pal = false;
            if (pal && (j - i + 1) > max_len) {
                max_len = j - i + 1;
                start = i;
            }
        }
    }
    cout << "\nLongest Palindrome: ";
    for (int i = start; i < start + max_len; i++) cout << data[i];
    cout << " (Length: " << max_len << ")" << endl;
}

// ==================== OPENGL ====================

void draw_text(float x, float y, const char* text) {
    glRasterPos2f(x, y);
    for (int i = 0; text[i]; i++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, text[i]);
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Background color
    glBegin(GL_QUADS);
        glColor3f(0.6f, 0.8f, 0.95f);
        glVertex2f(-1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
        glVertex2f(1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
    glEnd();


    if (!current_signal || signal_length == 0) { glFlush(); return; }

    float x_start = -0.9f, x_end = 0.9f;
    float y_high = 0.4f, y_low = -0.4f;
    float step = (x_end - x_start) / signal_length;

    // Grid
    glColor3f(0.8f, 0.85f, 0.9f);
    glBegin(GL_LINES);
    for (int i = 0; i <= signal_length; i++) {
        float x = x_start + i * step;
        glVertex2f(x, -0.5f);
        glVertex2f(x, 0.5f);
    }
    glEnd();

    // Axes
    glColor3f(0, 0, 0);
    glLineWidth(2);
    glBegin(GL_LINES);
        glVertex2f(x_start, 0.0f);
        glVertex2f(x_end, 0.0f);
    glEnd();

    // Title
    glColor3f(0.1f, 0.1f, 0.1f);
    draw_text(-0.3f, 0.7f, signal_title);

    // Signal line (vibrant color)
    glColor3f(0.4f, 0.0f, 0.7f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < signal_length; i++) {
        float x1 = x_start + i * step;
        float x2 = x_start + (i + 1) * step;
        float y = (current_signal[i] == 1) ? y_high : y_low;

        glVertex2f(x1, y);
        glVertex2f(x2, y);

        if (i < signal_length - 1 && current_signal[i] != current_signal[i+1]) {
            glVertex2f(x2, y);
            glVertex2f(x2, (current_signal[i+1] == 1) ? y_high : y_low);
        }
    }
    glEnd();

    // Bit labels
    for (int i = 0; i < signal_length; i++) {
        char bit[3];
        sprintf(bit, "%d", current_signal[i]);
        float x = x_start + (i + 0.4f) * step;
        draw_text(x, -0.6f, bit);
    }

    glFlush();
}

void init_gl() {
    glClearColor(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.0, 1.0, -1.0, 1.0);
}

void display_signal(int* signal, int len, const char* title, bool manchester) {
    current_signal = signal;
    signal_length = len;
    strcpy(signal_title, title);
    is_manchester_encoding = manchester;
    glutPostRedisplay();
}


int main(int argc, char** argv) {
    cout << "=== Digital Signal Generator ===\n";
    cout << "1. Digital Input\n2. Analog Input (PCM/DM)\nChoice: ";

    int input_type;
    cin >> input_type;

    char data[1000];
    int data_len = 0;

    if (input_type == 2) {
        cout << "\n1. PCM\n2. Delta Modulation\nChoice: ";
        int mod_choice; cin >> mod_choice;

        int samples;
        cout << "Enter number of samples: ";
        cin >> samples;

        double* analog_signal = new double[samples];
        cout << "Enter values: ";
        for (int i = 0; i < samples; i++) cin >> analog_signal[i];

        if (mod_choice == 1) {
            int bits;
            cout << "Bits per sample: ";
            cin >> bits;
            data_len = pcm_encode(analog_signal, samples, data, bits);
            cout << "\nPCM: " << data << endl;
        } else {
            data_len = delta_modulation(analog_signal, samples, data);
            cout << "\nDelta Modulation: " << data << endl;
        }
        delete[] analog_signal;
    } else {
        cout << "Enter binary data: ";
        cin >> data;
        data_len = strlen(data);
    }

    find_longest_palindrome(data, data_len);

    cout << "\nEncoding Schemes:\n1. NRZ-L\n2. NRZ-I\n3. Manchester\n4. Diff Manchester\n5. AMI\nChoice: ";
    int choice; cin >> choice;

    int* encoded = NULL;
    int encoded_len = 0;
    bool manchester = false;
    char title[100];

    switch (choice) {
        case 1: encoded_len = data_len; encoded = new int[encoded_len];
                encode_NRZ_L(data, encoded, data_len);
                strcpy(title, "NRZ-L Encoding"); break;
        case 2: encoded_len = data_len; encoded = new int[encoded_len];
                encode_NRZ_I(data, encoded, data_len);
                strcpy(title, "NRZ-I Encoding"); break;
        case 3: encoded_len = data_len * 2; encoded = new int[encoded_len];
                encode_MANCHESTER(data, encoded, data_len);
                strcpy(title, "Manchester Encoding"); manchester = true; break;
        case 4: encoded_len = data_len * 2; encoded = new int[encoded_len];
                encode_DIFF_MANCHESTER(data, encoded, data_len);
                strcpy(title, "Differential Manchester"); manchester = true; break;
        case 5: encoded_len = data_len; encoded = new int[encoded_len];
                encode_AMI(data, encoded, data_len);
                strcpy(title, "AMI Encoding");
                cout << "Apply Scrambling? (1=Yes, 0=No): ";
                int scr; cin >> scr;
                if (scr == 1) {
                    cout << "1. B8ZS\n2. HDB3\nChoice: ";
                    int s; 
                    cin >> s;
                    if (s == 1) { apply_B8ZS(encoded, encoded_len); strcpy(title, "AMI + B8ZS"); }
                    else { apply_HDB3(encoded, encoded_len); strcpy(title, "AMI + HDB3"); }
                }
                break;
        default: cout << "Invalid!\n"; return 1;
    }

    cout << "\nSignal: ";
    for (int i = 0; i < encoded_len; i++) cout << encoded[i] << " ";
    cout << endl;

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
    glutInitWindowSize(1200, 700);
    glutCreateWindow("Digital Signal Visualization");

    init_gl();
    display_signal(encoded, encoded_len, title, manchester);
    glutDisplayFunc(display);
    glutMainLoop();

    delete[] encoded;
    return 0;
}
