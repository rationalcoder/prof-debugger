#ifndef BUCKET_ARRAY_HPP
#define BUCKET_ARRAY_HPP
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <forward_list>
#include <cassert>

template <typename T_, uint32_t BucketSize_>
class BucketArray
{
public:
    using Bucket = std::vector<T_>;
    using BucketList = std::forward_list<Bucket>;
    using Type = T_;
    static constexpr bucket_size() { return BucketSize_; }

public:
    BucketArray()
        : list_(1), size_(), bucketCount_(1)
    {}

    const T_& operator [](uint32_t i) const;
    T_& operator [](uint32_t i);

    uint32_t size() const { return size_; }
    uint32_t storage_size() const { return bucketCount_ * BucketSize_ * sizeof(T_); }
    uint32_t bucket_count() const { return bucketCount_; }

    void push_back(const T_& t);
    void push_back(T_&& t);

private:
    BucketList list_;
    uint32_t size_;
    uint32_t bucketCount_;
};

template <typename T_, uint32_t BucketSize_>
const T_& BucketArray<T_, BucketSize_>::operator[](uint32_t index) const
{
    assert(index <= size_);

    uint32_t fromFront = bucketCount_ - index/BucketSize_;
    auto iter = list_.begin();
    while (fromFront--) ++iter;

    return iter[index];
}

template <typename T_, uint32_t BucketSize_>
T_& BucketArray<T_, BucketSize_>::operator[](uint32_t index)
{
    assert(index <= size_);

    uint32_t fromFront = bucketCount_ - index/BucketSize_;
    auto iter = list_.begin();
    while (fromFront--) ++iter;

    return iter[index];
}

template <typename T_, uint32_t BucketSize_>
void BucketArray<T_, BucketSize_>::push_back(const T_& t)
{
    Bucket* currentBucket = &list_.front();
    if (currentBucket->size() == BucketSize_) {
        list_.emplace_front();
        currentBucket = &list_.front();
        currentBucket->reserve(BucketSize_);
        bucketCount_++;
    }

    currentBucket->push_back(t);
    size_++;
}

template <typename T_, uint32_t BucketSize_>
void BucketArray<T_, BucketSize_>::push_back(T_&& t)
{
    Bucket* currentBucket = &list_.front();
    if (currentBucket->size() == BucketSize_) {
        list_.emplace_front();
        currentBucket = &list_.front();
        currentBucket->reserve(BucketSize_);
        bucketCount_++;
    }

    currentBucket->push_back(t);
    size_++;
}

#endif // BUCKET_ARRAY_HPP
