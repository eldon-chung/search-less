#pragma once

class Model {

  public:
    Model() {
    }
    Model(Model const &) = delete;
    Model &operator=(Model const &) = delete;
    Model(Model &&) = delete;
    Model &operator=(Model &&) = delete;
    ~Model() {
    }
};