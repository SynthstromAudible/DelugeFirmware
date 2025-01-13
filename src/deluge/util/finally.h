#pragma once

namespace util {
template <typename F>
struct Finalizer {
	Finalizer(F f) : clean_{f} {}

	~Finalizer() {
		if (enabled_) {
			clean_();
		}
	}

	void disable() { enabled_ = false; };

private:
	F clean_;
	bool enabled_{true};
};

template <typename F>
Finalizer<F> finally(F f) {
	return Finalizer<F>(f);
}
} // namespace util
