// Split out from api.ts so mockApi.ts can throw/catch the same error type
// without creating a circular import between the two.
export class ApiError extends Error {
  status: number;
  code?: string;

  constructor(status: number, code: string | undefined, message: string) {
    super(message);
    this.status = status;
    this.code = code;
  }
}
