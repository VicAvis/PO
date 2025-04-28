from locust import HttpUser, task

class StaticWebUser(HttpUser):
    @task
    def index(self):
        self.client.get("/index.html")

    @task
    def page2(self):
        self.client.get("/page2.html")

    # @task
    # def nonExistent(self):
    #     self.client.get("/weird.html")