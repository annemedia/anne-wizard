#ifndef CONCURRENT_H
#define CONCURRENT_H

#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QVector>
#include <QtConcurrent>
#include <atomic>
#include <functional>
#include <type_traits>

class Concurrent : public QObject {
  Q_OBJECT
public:
  struct FutureInfo {
    QFuture<void> future;
    quint64 id;
  };

  template <typename Func, typename... Args>
  static auto run(QObject *parent, Func &&func, Args &&...args) -> QFutureWatcher<typename std::invoke_result_t<Func, Args...>> *;

  template <typename Func, typename OnFinished, typename... Args> static void withCallback(QObject *parent, Func &&func, OnFinished &&onFinished, Args &&...args);

  static void cancelAll();

private:
  static QVector<FutureInfo> &futures();
  static QMutex &mutex();
  static std::atomic<quint64> &nextId();
};

inline QVector<Concurrent::FutureInfo> &Concurrent::futures() {
  static QVector<FutureInfo> s_futures;
  return s_futures;
}

inline QMutex &Concurrent::mutex() {
  static QMutex s_mutex;
  return s_mutex;
}

inline std::atomic<quint64> &Concurrent::nextId() {
  static std::atomic<quint64> s_nextId{0};
  return s_nextId;
}

inline void Concurrent::cancelAll() {
  QMutexLocker locker(&mutex());
  for (auto &info : futures()) {
    info.future.cancel();
  }
  futures().clear();
}

template <typename Func, typename... Args>
auto Concurrent::run(QObject *parent, Func &&func, Args &&...args) -> QFutureWatcher<typename std::invoke_result_t<Func, Args...>> * {
  using ReturnType = typename std::invoke_result_t<Func, Args...>;
  QFutureWatcher<ReturnType> *watcher = new QFutureWatcher<ReturnType>(parent);

  QFuture<ReturnType> future = QtConcurrent::run(std::forward<Func>(func), std::forward<Args>(args)...);
  watcher->setFuture(future);

  {
    QMutexLocker locker(&mutex());
    FutureInfo info;
    info.future = future;
    info.id = nextId().fetch_add(1);
    futures().append(info);

    QObject::connect(watcher, &QFutureWatcher<ReturnType>::finished, parent, [id = info.id]() {
      QMutexLocker locker(&mutex());
      for (int i = 0; i < futures().size(); ++i) {
        if (futures()[i].id == id) {
          futures().remove(i);
          break;
        }
      }
    });
  }

  return watcher;
}

template <typename Func, typename OnFinished, typename... Args> void Concurrent::withCallback(QObject *parent, Func &&func, OnFinished &&onFinished, Args &&...args) {
  using ReturnType = typename std::invoke_result_t<Func, Args...>;

  QFutureWatcher<ReturnType> *watcher = new QFutureWatcher<ReturnType>(parent);

  QFuture<ReturnType> future = QtConcurrent::run(std::forward<Func>(func), std::forward<Args>(args)...);
  watcher->setFuture(future);

  quint64 currentId = 0;
  {
    QMutexLocker locker(&mutex());
    FutureInfo info;
    info.future = future;
    currentId = nextId().fetch_add(1);
    info.id = currentId;
    futures().append(info);
  }

  QObject::connect(watcher, &QFutureWatcher<ReturnType>::finished, parent, [watcher, onFinished = std::forward<OnFinished>(onFinished), currentId]() {
    {
      QMutexLocker locker(&mutex());
      for (int i = 0; i < futures().size(); ++i) {
        if (futures()[i].id == currentId) {
          futures().remove(i);
          break;
        }
      }
    }

    ReturnType result = watcher->result();
    onFinished(result);
    watcher->deleteLater();
  });
}

#endif