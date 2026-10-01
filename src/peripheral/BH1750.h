#pragma once

class BH1750 final {
  public:
    static BH1750& Instance();

    int Initialize();
    int ReadLux(float* lux);
    bool Healthy() const {
        return mHealthy;
    }

    BH1750(const BH1750&) = delete;
    BH1750& operator=(const BH1750&) = delete;

  private:
    BH1750() = default;
    ~BH1750() = default;

    bool mHealthy = false;
    unsigned int mErrorCount = 0;
};

inline BH1750* GetBH1750() {
    return &BH1750::Instance();
}
