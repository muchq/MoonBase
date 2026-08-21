declare const worker: {
  fetch(
    request: Request,
    env: { ASSETS: { fetch(request: Request): Promise<Response> } }
  ): Promise<Response>;
};
export default worker;
