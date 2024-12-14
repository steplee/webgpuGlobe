#pragma once

#include <vector>
#include <cstdint>

namespace wg {
	
	// A nearly drop-in replacement for cv::Mat

	struct ImagePtr {
		const uint8_t* data_ = 0;
		int cols=0, rows=0, channels_=0;

		const uint8_t* data() const { return data_; }
		int channels() const { return channels_; }
		int total() const { return cols * rows; }
		int elemSize() const { return channels_; }
		inline bool empty() const { return total() * elemSize() == 0; }
	};


	struct Image {

		std::vector<uint8_t> data_;
		int cols=0, rows=0, channels_=0;

		void allocate(int h, int w, int c) {
			rows = h;
			cols = w;
			channels_ = c;
			data_.resize(w*h*c);
		}

		uint8_t* data() { return data_.data(); }
		const uint8_t* data() const { return data_.data(); }
		int channels() const { return channels_; }
		int total() const { return cols * rows; }
		int elemSize() const { return channels_; }
		inline bool empty() const { return total() * elemSize() == 0; }

		ImagePtr toPtr() const {
			return ImagePtr { data(), cols, rows, channels_ };
		}
	};

}
