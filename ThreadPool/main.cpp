#include <ctime>
#include <cmath>
#include "parallel.hpp"
using namespace gk;
using std::vector;

static int sCount = 0;

struct Mat {
	uint8_t* data;
	int rows, cols;

	Mat(int r, int c)
		: data(nullptr)
	{
		if (r * c > 0) {
			data = new uint8_t[r * c];
			GK_ASSERT(data);
			rows = r;
			cols = c;
		} else {
			data = nullptr;
			rows = cols = 0;
		}
	}

	~Mat()
	{
		delete[] data;
		data = nullptr;
		rows = cols = -1;
	}
};

static void draw_mandelbrot(Mat& m,
	double x0, double y0, double ppi, int start, int stop)
{
	int const iteration = 300;
	for (int h = start; h < stop; ++h) {
		uint8_t* M = m.data + h * m.cols;
		double Y0 = y0 + h * ppi;
		for (int w = 0; w < m.cols; ++w) {
			double X0 = x0 + w * ppi;
			double x = 0, y = 0, t;
			double z = x * x + y * y;
			int iter = 0;
			while (z < 4 && iter < iteration) {
				++iter;
				t = x * x - y * y + X0;
				y = 2 * x * y + Y0;
				x = t;
				z = x * x + y * y;
			}
			// make gradient smoother
			if (z > 4)
				z = iter - log2(log2(z) * 0.5);
			else
				z = iter;
			z *= 255.0 / iteration;
			M[w] = static_cast<uint8_t>(z);
		}
	}
}

static void writePGM(Mat const& img, int frame)
{
	return;
	char name[128];
	snprintf(name, sizeof(name), "pool%02d.ppm", frame);
	FILE* fid = fopen(name, "wb");
	if (!fid) {
		perror(name);
		return;
	}
	fprintf(fid, "P5\n%d %d\n255\n", img.cols, img.rows);
	fwrite(img.data, sizeof(uint8_t), img.rows * img.cols, fid);
	fclose(fid);
}

class MbSync : public SyncJob {
	Mat m;
	int frame;
	double x0, y0, ppi;

public:
	MbSync(int rows, int cols, double ox, double oy, double radius)
		: m(rows, cols), frame(++sCount)
	{
		allstart = 0;
		allend = m.rows;
		ppi = 2 * radius / min(m.rows, m.cols);
		x0 = ox - (m.cols - 1) * 0.5 * ppi;
		y0 = oy - (m.rows - 1) * 0.5 * ppi;
	}

	~MbSync() { writePGM(m, frame); }

	void call(uint32_t /* tid */, uint32_t from, uint32_t to) override
	{
		draw_mandelbrot(m, x0, y0, ppi, from, to);
	}
};

class MbAsync : public AsyncJob {
	Mat m;
	int index, ntrd, frame;
	double x0, y0, ppi;

public:
	MbAsync(int num_thread, int rows, int cols, double ox, double oy, double radius)
		: m(rows, cols), index(0), ntrd(num_thread), frame(++sCount)
	{
		ppi = 2 * radius / min(m.rows, m.cols);
		x0 = ox - (m.cols - 1) * 0.5 * ppi;
		y0 = oy - (m.rows - 1) * 0.5 * ppi;
	}

	~MbAsync() { writePGM(m, frame); }

	void call() override
	{
		if (ntrd <= 1) {
			draw_mandelbrot(m, x0, y0, ppi, 0, m.rows);
			return;
		}
		while (true) {
			int start = atomic_load(&index);
			if (start >= m.cols)
				break;
			// starting from 1/2, you can compare it with syncjob (1/4)
			int stripe = max(1, (m.rows - start) / ntrd / 2);
			start = atomic_fetch_add(&index, stripe);
			draw_mandelbrot(m, x0, y0, ppi, start, min(start + stripe, m.rows));
		}
	}
};

int main()
{
	fputs("├Hello, World┤\n", stdout);
	double ifreq = 1e3 / getTickFrequency();
	int const TRD[] = {4, 1, 0, 8, 1, 16, 7, 6};
	int const nTRD = sizeof(TRD) / sizeof(TRD[0]);
	int const rows = 1600, cols = 2000;
	double const X0 = 0.27322626, Y0 = 0.595153338;

	{
		SyncPool pool;
		for (int i = 0; i < nTRD; ++i) {
			fprintf(stdout, " SyncPool set thread to %2d", TRD[i]);
			pool.setNumThread(TRD[i]);
			fprintf(stdout, " done\n");
			Sleep(100);
		}
		for (int i = 0; i < 7; ++i) {
			double x = -0.75, y = 0, r = 1.5;
			if (i > 0)
				x = X0, y = Y0, r = pow(0.2, i);
			int64_t t1 = getTickCount();
			MbSync mb(rows, cols, x, y, r);
			pool.submit(mb);
			double elapse = static_cast<double>(getTickCount() - t1) * ifreq;
			fprintf(stdout,
				" SyncPool %d x % 9.6f y % 9.7f r %9.7f t %9.3f\n",
				i, x, y, r, elapse);
		}
	}

	{
		AsyncPool pool;
		for (int i = 0; i < nTRD; ++i) {
			fprintf(stdout, "AsyncPool set thread to %2d", TRD[i]);
			pool.setNumThread(TRD[i]);
			fprintf(stdout, " done\n");
			Sleep(100);
		}
		int64_t t1 = getTickCount();
		for (int i = 0; i < 7; ++i) {
			double x = -0.75, y = 0, r = 1.5;
			if (i > 0)
				x = X0, y = Y0, r = pow(0.2, i);
			pool.submit(std::make_shared<MbAsync>(0, rows, cols, x, y, r));
		}
		pool.wait();
		double elapse = static_cast<double>(getTickCount() - t1) * ifreq;
		fprintf(stdout, "AsyncPool %d t %9.3f\n", 7, elapse);
	}

	{
		AsyncPool pool;
		int const ntrd = TRD[nTRD - 1];
		pool.setNumThread(ntrd);
		for (int i = 0; i < 7; ++i) {
			double x = -0.75, y = 0, r = 1.5;
			if (i > 0)
				x = X0, y = Y0, r = pow(0.2, i);
			int64_t t1 = getTickCount();
			auto mb = std::make_shared<MbAsync>(ntrd, rows, cols, x, y, r);
			// submit at least 1 times
			for (int t = max(ntrd, 1); t--;)
				pool.submit(mb);
			mb->wait();
			double elapse = static_cast<double>(getTickCount() - t1) * ifreq;
			fprintf(stdout,
				"AsyncPool %d x % 9.6f y % 9.7f r %9.7f t %9.3f\n",
				i, x, y, r, elapse);
		}
	}
}
